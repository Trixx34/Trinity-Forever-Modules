/*
 * Bot lifecycle. See BotMgr.h.
 */

#include "BotMgr.h"
#include "BotProfessions.h"
#include "BotTalentSpec.h"
#include "BotTaxi.h"
#include "AccountMgr.h"
#include "CharacterCache.h"
#include "CharacterPackets.h"
#include "Config.h"
#include "DB2Stores.h"
#include "CryptoRandom.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "ModuleBotSnapshot.h"
#include "ModuleMgr.h"
#include "Map.h"
#include "MovementPackets.h"
#include "WorldPacket.h"
#include "Opcodes.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "RealmList.h"
#include "Random.h"
#include "ScriptMgr.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "StringConvert.h"
#include "StringFormat.h"
#include "Trainer.h"
#include "TraitMgr.h"
#include "TraitPackets.h"
#include "Util.h"
#include "World.h"
#include "WorldSession.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <ctime>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <thread>
#include <unordered_map>

namespace
{

constexpr char const* BOT_ACCOUNT_PREFIX = "BOT";
constexpr char const* BOT_ACCOUNT_EMAIL = "bot@forever.invalid";

char const* const NameStart[] = { "bal", "bel", "dor", "fen", "gal", "hal", "jor", "kar", "lor", "mar", "nor", "ral", "sal", "tor", "val", "zan", "ur", "vor" };
char const* const NameMiddle[] = { "a", "e", "i", "o", "an", "or", "el", "in" };
char const* const NameEnd[] = { "an", "ar", "en", "il", "on", "or", "us", "ia", "eth", "dan", "wyn", "mir", "gar" };

template<typename T, size_t N>
T const& RandomOf(T const (&values)[N])
{
    return values[urand(0, N - 1)];
}

// Random syllables form a valid player name without exposing the internal account prefix. Up to two middle syllables: with one, the pool was
// about 2100 names and a sim with 2000 bots used them all up (no free name); with two it is about 17000.
std::string GenerateBotName()
{
    std::string name = std::string(RandomOf(NameStart));
    for (uint32 middles = urand(0, 2); middles > 0; --middles)
        name += RandomOf(NameMiddle);
    name += RandomOf(NameEnd);
    if (name.size() > 12)
        name.resize(12);
    normalizePlayerName(name);
    return name;
}

bool IsHordeRace(uint8 raceId)
{
    return Player::TeamForRace(raceId) == HORDE;
}

// A session with no socket, owned by BotMgr. Same constructor arguments as the stock session the world creates for a login.
WorldSession* MakeBotSession(uint32 accountId, std::string name)
{
    WorldSession* session = new WorldSession(accountId, std::move(name), 0, std::string(), nullptr, SEC_PLAYER, uint8(0), time_t(0),
        std::string(), Minutes(0), 0u, ClientBuild::VariantId{}, LOCALE_enUS, 0u, false);
    session->SetBot();
    return session;
}

} // namespace

BotMgr* BotMgr::instance()
{
    static BotMgr instance;
    return &instance;
}

namespace
{

// Seconds since local midnight for "HH:MM" or "HH:MM:SS"; -1 if the text is not a clock time.
int32 ParseClockSeconds(std::string const& text)
{
    if (text.size() != 5 && text.size() != 8)
        return -1;
    if (text[2] != ':' || (text.size() == 8 && text[5] != ':'))
        return -1;

    auto field = [&text](size_t pos, int32 max) -> int32
    {
        if (text[pos] < '0' || text[pos] > '9' || text[pos + 1] < '0' || text[pos + 1] > '9')
            return -1;
        int32 const value = (text[pos] - '0') * 10 + (text[pos + 1] - '0');
        return value <= max ? value : -1;
    };

    int32 const hours = field(0, 23);
    int32 const minutes = field(3, 59);
    int32 const seconds = text.size() == 8 ? field(6, 59) : 0;
    if (hours < 0 || minutes < 0 || seconds < 0)
        return -1;
    return hours * 3600 + minutes * 60 + seconds;
}

// Bots stand on a sunflower spiral around their race's start: slot s is at radius spacing * sqrt(s) and angle s times the
// golden angle, so neighbouring slots are a fixed distance apart and no two bots share a point. A bot's slot comes from its
// guid, so it is the same after every restart. 4096 slots keep 1500 bots apart with no slot shared, and the largest radius
// (spacing * 64) is the only cap the spiral needs.
constexpr uint32 kSpreadSlots = 4096;
constexpr float kGoldenAngle = 2.39996323f;
// A bot is spread only while it is still within this distance of its race's create spot. A bot that has moved keeps its place.
constexpr float kCreateSpotYards = 1.0f;
// Candidate points tried for a slot before a bot is left where it is: each turns the angle and pulls the radius in.
constexpr uint32 kSpreadAttempts = 8;
// The ground under a candidate may differ from the start's height by at most this much (a ledge is not a place to stand).
constexpr float kSpreadMaxDropYards = 4.0f;

int32 LocalSecondsOfDay()
{
    time_t const now = time(nullptr);
    tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    return local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec;
}

// Bot.Dump.SpellCosts = <file>: once at startup, one line per spell that costs power, so a module can tell before a cast whether the bot can pay for it
// (the bots module reads the file as SpellCostFile). Tab separated: spell id, the level the spell needs, the power type (0 mana, 1 rage, 3 energy...),
// the flat cost, the cost added per level, the percent cost (of the base mana, 0 when none), the next rank's spell id (0 for the last), the name. The
// costs are in the core's own units, the ones Unit::GetPower uses (rage is tenths of a point), so they compare with the bot's power as it is.
void DumpSpellCosts(std::string const& path)
{
    std::ofstream out(path, std::ios::trunc);
    if (!out)
    {
        TC_LOG_ERROR("server.worldserver", "BotMgr: cannot write the spell costs to {}", path);
        return;
    }
    out << "# spell_id\tlevel\tpower_type\tcost\tcost_per_level\tcost_pct\tnext_rank\tname\n";
    uint32 lines = 0;
    sSpellMgr->ForEachSpellInfo([&](SpellInfo const* info)
    {
        for (SpellPowerEntry const* power : info->PowerCosts)
        {
            if (!power || (power->ManaCost == 0 && power->PowerCostPct == 0.0f))
                continue;
            SpellInfo const* next = info->GetNextRankSpell();
            out << info->Id << '\t' << info->SpellLevel << '\t' << int32(power->PowerType) << '\t' << power->ManaCost << '\t' << power->ManaCostPerLevel << '\t'
                << power->PowerCostPct << '\t' << (next ? next->Id : 0) << '\t'
                << (info->SpellName ? std::string((*info->SpellName)[sWorld->GetDefaultDbcLocale()]) : std::string("?")) << '\n';
            ++lines;
        }
    });
    TC_LOG_INFO("server.worldserver", "BotMgr: wrote the costs of {} spell power entries to {}", lines, path);
}

// Bot.Dump.Recipes = <file>: once at startup, one line per profession recipe (a spell of a profession's skill line), so crafting can be planned from the game
// data: the spell id, the skill line, the skill it needs, where it turns green and grey, the spell focus it needs (3 = a cooking fire), the item it creates
// and how many, the reagents (item:count, joined by ;) and the name. Spells named Campfire, Flint or Tinder are listed as well, with skill 0: what a campfire
// needs is not in a skill line.
void DumpRecipes(std::string const& path)
{
    std::ofstream out(path, std::ios::trunc);
    if (!out)
    {
        TC_LOG_ERROR("server.worldserver", "BotMgr: cannot write the recipes to {}", path);
        return;
    }
    static constexpr std::array<uint16, 15> professions = { 164, 165, 171, 182, 186, 197, 202, 333, 393, 129, 185, 356, 755, 773, 794 };
    out << "# spell_id\tskill\tmin_skill\ttrivial_low\ttrivial_high\tfocus\tcreates_item\tcreates_count\treagents\tname\n";
    uint32 lines = 0;
    sSpellMgr->ForEachSpellInfo([&](SpellInfo const* info)
    {
        std::string const name = info->SpellName ? std::string((*info->SpellName)[sWorld->GetDefaultDbcLocale()]) : std::string("?");
        bool const campfire = name.find("Campfire") != std::string::npos || name.find("Flint") != std::string::npos || name.find("Tinder") != std::string::npos;

        uint32 createdItem = 0, createdCount = 0;
        for (SpellEffectInfo const& effect : info->GetEffects())
            if (effect.IsEffect(SPELL_EFFECT_CREATE_ITEM))
            {
                createdItem = effect.ItemType;
                createdCount = uint32(std::max<int32>(1, int32(effect.BasePoints) + 1));
                break;
            }
        std::string reagents;
        for (std::size_t i = 0; i < info->Reagent.size(); ++i)
            if (info->Reagent[i] > 0)
                reagents += (reagents.empty() ? "" : ";") + std::to_string(info->Reagent[i]) + ":" + std::to_string(std::max<int32>(1, info->ReagentCount[i]));

        auto const bounds = sSpellMgr->GetSkillLineAbilityMapBounds(info->Id);
        bool wrote = false;
        for (auto it = bounds.first; it != bounds.second; ++it)
        {
            SkillLineAbilityEntry const* ability = it->second;
            if (std::find(professions.begin(), professions.end(), ability->SkillLine) == professions.end())
                continue;
            out << info->Id << '\t' << ability->SkillLine << '\t' << ability->MinSkillLineRank << '\t' << ability->TrivialSkillLineRankLow << '\t'
                << ability->TrivialSkillLineRankHigh << '\t' << info->RequiresSpellFocus << '\t' << createdItem << '\t' << createdCount << '\t' << reagents << '\t' << name << '\n';
            ++lines;
            wrote = true;
        }
        if (!wrote && campfire)
        {
            out << info->Id << "\t0\t0\t0\t0\t" << info->RequiresSpellFocus << '\t' << createdItem << '\t' << createdCount << '\t' << reagents << '\t' << name << '\n';
            ++lines;
        }
    });
    TC_LOG_INFO("server.worldserver", "BotMgr: wrote {} recipes to {}", lines, path);
}

// Bot.Dump.Talents = <file>: once at startup, one line per talent node entry (a choice a node offers), so the talent profiles can be checked against the
// server's own data: the node and its tree and sub-tree, its position, the node entry, its trait definition, the spell the definition grants, the max ranks,
// the entry type and the spell name. The costs are not in the dump: the cost tables are empty on the test data.
void DumpTalents(std::string const& path)
{
    std::ofstream out(path, std::ios::trunc);
    if (!out)
    {
        TC_LOG_ERROR("server.worldserver", "BotMgr: cannot write the talents to {}", path);
        return;
    }
    out << "# node\ttree\tsub_tree\tpos_x\tpos_y\tnode_type\tentry\tdefinition\tspell\tmax_ranks\tentry_type\tname\n";
    uint32 lines = 0;
    for (TraitNodeXTraitNodeEntryEntry const* link : sTraitNodeXTraitNodeEntryStore)
    {
        TraitNodeEntry const* node = sTraitNodeStore.LookupEntry(link->TraitNodeID);
        TraitNodeEntryEntry const* entry = sTraitNodeEntryStore.LookupEntry(link->TraitNodeEntryID);
        if (!node || !entry)
            continue;

        uint32 spellId = 0;
        std::string name = "?";
        if (TraitDefinitionEntry const* definition = sTraitDefinitionStore.LookupEntry(entry->TraitDefinitionID))
            if (SpellInfo const* info = sSpellMgr->GetSpellInfo(definition->SpellID, DIFFICULTY_NONE))
            {
                spellId = info->Id;
                if (info->SpellName)
                    name = std::string((*info->SpellName)[sWorld->GetDefaultDbcLocale()]);
            }

        out << node->ID << '\t' << node->TraitTreeID << '\t' << node->TraitSubTreeID << '\t' << node->PosX << '\t' << node->PosY << '\t' << int32(node->Type) << '\t'
            << entry->ID << '\t' << entry->TraitDefinitionID << '\t' << spellId << '\t' << entry->MaxRanks << '\t' << int32(entry->NodeEntryType) << '\t' << name << '\n';
        ++lines;
    }
    TC_LOG_INFO("server.worldserver", "BotMgr: wrote {} talent node entries to {}", lines, path);
}

// Talent profiles (Bot.Talents.Profiles, a file from scripts/gen-talent-profile-file.py): one line per (class id, key),
// "<classId> <key> <nodeId>:<ranks>,<nodeId>:<ranks>,...", read once at startup. The key is a role (tank, healer, dps) or, since protocol 18, one of the
// class's spec keys (BotTalentSpec.h: arms, bm, feral_cat, ...). The map key is the pair (class id, BotTalentSpec profile key byte).
std::map<std::pair<uint32, uint8>, std::vector<TraitMgr::ProfileStep>> g_talentProfiles;
// bot guid counter -> the (role, spec) a module last gave it (world thread only)
std::unordered_map<uint64, std::pair<uint8, uint8>> g_talentRoles;

void LoadTalentProfiles(std::string const& path)
{
    std::ifstream in(path);
    if (!in)
    {
        TC_LOG_ERROR("server.worldserver", "BotMgr: cannot open Bot.Talents.Profiles file {}", path);
        return;
    }
    std::string line;
    uint32 lineNo = 0;
    while (std::getline(in, line))
    {
        ++lineNo;
        if (line.empty() || line[0] == '#')
            continue;
        std::vector<std::string_view> const parts = Trinity::Tokenize(line, ' ', false);
        Optional<uint32> const classId = parts.size() == 3 ? Trinity::StringTo<uint32>(parts[0]) : Optional<uint32>();
        uint8 key = 0;
        if (classId && *classId)
            key = BotTalentSpec::ProfileKeyFromName(*classId, parts[1]);
        std::vector<TraitMgr::ProfileStep> steps;
        bool ok = classId && *classId && key;
        if (ok)
        {
            for (std::string_view step : Trinity::Tokenize(parts[2], ',', false))
            {
                std::vector<std::string_view> const pair = Trinity::Tokenize(step, ':', false);
                Optional<int32> const node = pair.size() == 2 ? Trinity::StringTo<int32>(pair[0]) : Optional<int32>();
                Optional<int32> const ranks = pair.size() == 2 ? Trinity::StringTo<int32>(pair[1]) : Optional<int32>();
                if (!node || !ranks || *node <= 0 || *ranks <= 0)
                {
                    ok = false;
                    break;
                }
                steps.push_back({ *node, *ranks });
            }
            ok = ok && !steps.empty();
        }
        if (!ok)
        {
            TC_LOG_ERROR("server.worldserver", "BotMgr: {} line {} is not '<classId> <tank|healer|dps|a spec key of that class> <node>:<ranks>,...', skipped", path, lineNo);
            continue;
        }
        g_talentProfiles[{ *classId, key }] = std::move(steps);
    }
    TC_LOG_INFO("server.worldserver", "BotMgr: loaded {} talent profile(s) from {}", g_talentProfiles.size(), path);
}

} // namespace

void BotMgr::LoadRegistry()
{
    if (_registryLoaded)
        return;
    _registryLoaded = true;

    if (std::string const costFile = sConfigMgr->GetStringDefault("Bot.Dump.SpellCosts", ""); !costFile.empty())
        DumpSpellCosts(costFile);
    if (std::string const recipeFile = sConfigMgr->GetStringDefault("Bot.Dump.Recipes", ""); !recipeFile.empty())
        DumpRecipes(recipeFile);
    if (std::string const talentFile = sConfigMgr->GetStringDefault("Bot.Dump.Talents", ""); !talentFile.empty())
        DumpTalents(talentFile);
    if (std::string const profileFile = sConfigMgr->GetStringDefault("Bot.Talents.Profiles", ""); !profileFile.empty())
        LoadTalentProfiles(profileFile);

    _loginMaxPerTick = uint32(std::max<int32>(1, sConfigMgr->GetIntDefault("Bot.Login.MaxPerTick", 5)));
    _loginTimeoutMs = uint32(std::max<int32>(1000, sConfigMgr->GetIntDefault("Bot.Login.TimeoutMs", 60000)));
    _reloginDelayMs = uint32(std::max<int32>(0, sConfigMgr->GetIntDefault("Bot.Relogin.DelayMs", 2000)));
    _mailCollect = sConfigMgr->GetBoolDefault("Bot.Mail.Collect", true);
    _mailCollectEveryMs = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.Mail.CollectEverySec", 30), 5, 3600)) * 1000;
    _spreadSpacing = std::max(0.0f, sConfigMgr->GetFloatDefault("Bot.Spread.SpacingYards", 3.0f));
    _spreadMaxRadius = std::max(0.0f, sConfigMgr->GetFloatDefault("Bot.Spread.MaxRadiusYards", 200.0f));
    _loginPerSecond = uint32(std::max<int32>(1, sConfigMgr->GetIntDefault("Bot.Login.MaxPerSecond", 10)));
    _limitMaxBots = uint32(std::max<int32>(1, sConfigMgr->GetIntDefault("Bot.Limit.MaxBots", 1500)));
    _limitMaxWorldDiffMs = uint32(std::max<int32>(10, sConfigMgr->GetIntDefault("Bot.Limit.MaxWorldDiffMs", 250)));
    _limitMaxDbQueue = uint32(std::max<int32>(1, sConfigMgr->GetIntDefault("Bot.Limit.MaxDbQueue", 5000)));

    // The run's stop time is the first such local clock time after startup. At that point no bot is created, and the
    // server shuts down through the normal shutdown path, so the bots are saved and logged out.
    std::string const stopText = sConfigMgr->GetStringDefault("Bot.Run.StopTime", "");
    if (!stopText.empty())
    {
        int32 const stopSec = ParseClockSeconds(stopText);
        if (stopSec < 0)
        {
            TC_LOG_ERROR("server.worldserver", "Bot.Run.StopTime '{}' is not HH:MM or HH:MM:SS; the run has no stop time.", stopText);
        }
        else
        {
            int32 untilStop = (stopSec - LocalSecondsOfDay() + 86400) % 86400;
            if (untilStop == 0)
                untilStop = 86400;
            _stopConfigured = true;
            _stopAtMs = uint32(untilStop) * 1000;
            TC_LOG_INFO("server.worldserver", "Bot run stops at {} local, in {} minutes.", stopText, untilStop / 60);
        }
    }

    std::vector<uint32> accounts;
    std::map<uint32, std::string> accountNames;
    if (QueryResult result = LoginDatabase.PQuery("SELECT id, username FROM account WHERE username LIKE '{}%'", BOT_ACCOUNT_PREFIX))
    {
        size_t const prefixLength = strlen(BOT_ACCOUNT_PREFIX);
        do
        {
            Field* fields = result->Fetch();
            std::string username = fields[1].GetString();
            Optional<uint32> number = username.size() > prefixLength ? Trinity::StringTo<uint32>(username.substr(prefixLength)) : std::nullopt;
            if (!number)
                continue;

            accounts.push_back(fields[0].GetUInt32());
            accountNames.emplace(fields[0].GetUInt32(), username);
            _nextAccountNumber = std::max(_nextAccountNumber, *number + 1);
        } while (result->NextRow());
    }

    for (size_t begin = 0; begin < accounts.size(); begin += 400)
    {
        std::string list;
        for (size_t i = begin; i < std::min(accounts.size(), begin + 400); ++i)
            list += (list.empty() ? "" : ",") + std::to_string(accounts[i]);

        QueryResult result = CharacterDatabase.PQuery("SELECT guid, account, name, race, class, gender, level FROM characters WHERE account IN ({})", list);
        if (!result)
            continue;

        do
        {
            Field* fields = result->Fetch();
            BotInfo bot;
            bot.Guid = fields[0].GetUInt64();
            bot.AccountId = fields[1].GetUInt32();
            bot.AccountName = accountNames[bot.AccountId];
            bot.Name = fields[2].GetString();
            bot.Race = fields[3].GetUInt8();
            bot.Class = fields[4].GetUInt8();
            bot.Gender = fields[5].GetUInt8();
            bot.Level = fields[6].GetUInt8();
            bot.Horde = IsHordeRace(bot.Race);
            _bots.emplace(bot.Guid, std::move(bot));
        } while (result->NextRow());
    }

    std::set<uint32> usedAccounts;
    for (auto const& [guid, bot] : _bots)
        usedAccounts.insert(bot.AccountId);
    for (auto const& [accountId, username] : accountNames)
        if (!usedAccounts.contains(accountId))
            _freeAccounts.emplace_back(accountId, username);

    TC_LOG_INFO("server.worldserver", "BotMgr: {} bot accounts, {} bot characters found", accounts.size(), _bots.size());
}

namespace
{

// Set while DumpAbilities levels its probe, so BotMgr::OnBotLevelChanged leaves it alone (the dump teaches the trainer spells itself, without
// the skills and talents).
bool g_dumpingAbilities = false;

// Teaches a bot every trainer spell it qualifies for, for free (never tradeskill or pet trainers). Several passes, since learning one spell can
// make the next rank available. Returns how many were taught. Used when a bot levels up, and by the ability dump so the dump shows the class
// abilities a bot really has, not only the starter spells.
uint32 TeachTrainerSpells(Player& bot)
{
    uint32 taught = 0;
    for (int pass = 0; pass < 8; ++pass)
    {
        uint32 learned = 0;
        for (auto const& [id, trainer] : sObjectMgr->GetTrainers())
            if (trainer.GetType() != Trainer::Type::Tradeskill && trainer.GetType() != Trainer::Type::Pet
                && BotProfessions::TrainerMayTeach(trainer, bot.GetGUID().GetCounter()))
                learned += trainer.TeachAvailableSpellsFree(&bot);
        taught += learned;
        if (!learned)
            break;
    }
    return taught;
}

// A full set of bags for a bot: Bot.Bags.Item (default 3914, Journeyman's Backpack) in each of the first Bot.Bags.Count (default 4, at most
// 4) empty bag slots. A new character has one small backpack, and bots that bring home quest rewards and loot fill it within minutes (the soak
// saw hundreds of thousands of "bags full" refusals). Returns how many bags were put on. Bot.Bags.Item = 0 turns it off.
uint32 GiveBags(Player& bot)
{
    static uint32 const itemId = uint32(std::max<int64>(0, sConfigMgr->GetIntDefault("Bot.Bags.Item", 3914)));
    static uint32 const wanted = uint32(std::clamp<int64>(sConfigMgr->GetIntDefault("Bot.Bags.Count", 4), 0, INVENTORY_SLOT_BAG_END - INVENTORY_SLOT_BAG_START));
    if (!itemId)
        return 0;

    uint32 given = 0;
    for (uint8 slot = INVENTORY_SLOT_BAG_START; slot < INVENTORY_SLOT_BAG_START + wanted; ++slot)
    {
        if (bot.GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            continue;
        uint16 dest = 0;
        if (bot.CanEquipNewItem(slot, dest, itemId, false) != EQUIP_ERR_OK)
            continue;
        if (bot.EquipNewItem(dest, itemId, ItemContext::NONE, true))
            ++given;
    }
    return given;
}

// One stack of starting ammunition for a newly created hunter (never repeated on login). Arrows for a bow or crossbow, bullets for a gun; nothing
// when there is no ammo-using ranged weapon. Item entry is chosen at runtime from the world's ItemTemplate data, not hardcoded. The
// amount is one maximum-size stack (default 200), not a full bag.
uint32 GiveStarterAmmo(Player& bot)
{
    if (!bot.NeedsAmmo())
        return 0;

    static uint32 const stacks = uint32(std::clamp<int64>(sConfigMgr->GetIntDefault("Bot.Hunter.StartAmmo.Stacks", 1), 1, 10));
    ItemTemplate const* chosen = nullptr;
    for (auto const& [entry, proto] : sObjectMgr->GetItemTemplateStore())
    {
        if (proto.GetClass() != ITEM_CLASS_PROJECTILE || !bot.IsAmmoUsableWithRangedWeapon(&proto) || bot.CanUseItem(&proto) != EQUIP_ERR_OK)
            continue;
        if (!chosen || proto.GetBaseRequiredLevel() < chosen->GetBaseRequiredLevel() ||
            (proto.GetBaseRequiredLevel() == chosen->GetBaseRequiredLevel() && entry < chosen->GetId()))
            chosen = &proto;
    }
    if (!chosen)
        return 0;

    uint32 const count = chosen->GetMaxStackSize() * stacks;
    ItemPosCountVec destination;
    if (bot.CanStoreNewItem(NULL_BAG, NULL_SLOT, destination, chosen->GetId(), count) != EQUIP_ERR_OK)
        return 0;
    Item* item = bot.StoreNewItem(destination, chosen->GetId(), true);
    if (!item)
        return 0;

    bot.SetAmmo(chosen->GetId());
    TC_LOG_INFO("server.worldserver", "BotMgr: bot {} (hunter) was given {} x item {} (max-stack starter ammo) for its ranged weapon.", bot.GetName(), count, chosen->GetId());
    return count;
}


// Logs the spells a new character of this race and class has at each level, as the core itself grants them: a Player::Create, then
// GiveLevel up through the levels, then the free trainer spells (the same calls CreateBot and OnBotLevelChanged make for a bot), on an
// unsaved player that is never in the world. Each line lists the spells gained at that level, with their names, so it shows what a bot can use. Only for finding out
// what bots have (Bot.Dump.Abilities); it creates a second, temporary player per race and class.
void DumpAbilities(WorldSession* session, WorldPackets::Character::CharacterCreateInfo const& createInfo)
{
    struct Guard { Guard() { g_dumpingAbilities = true; } ~Guard() { g_dumpingAbilities = false; } } guard;
    auto deleter = [](Player* player)
    {
        player->CleanupsBeforeDelete();
        delete player;
    };
    std::unique_ptr<Player, decltype(deleter)> probe(new Player(session), deleter);
    probe->GetMotionMaster()->Initialize();
    if (!probe->Create(sObjectMgr->GetGenerator<HighGuid::Player>().Generate(), &createInfo))
        return;

    static constexpr uint8 Levels[] = { 1, 5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60 };
    uint32 const maxLevel = sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL);

    std::set<uint32> known;
    for (uint8 level : Levels)
    {
        if (level > maxLevel)
            break;
        if (level > probe->GetLevel())
            probe->GiveLevel(level);
        TeachTrainerSpells(*probe);

        std::vector<uint32> gained;
        for (auto const& entry : probe->GetSpellMap())
            if (entry.second.state != PLAYERSPELL_REMOVED && known.insert(entry.first).second)
                gained.push_back(entry.first);
        std::sort(gained.begin(), gained.end());

        std::string list;
        for (uint32 spellId : gained)
        {
            SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE);
            list += (list.empty() ? "" : "; ") + std::to_string(spellId) + " " + (info && info->SpellName ? std::string((*info->SpellName)[sWorld->GetDefaultDbcLocale()]) : std::string("?"));
        }
        TC_LOG_INFO("server.worldserver", "BotAbilities: race {} class {} level {}: {} new spell(s), {} in total: {}",
            uint32(createInfo.Race), uint32(createInfo.Class), uint32(probe->GetLevel()), gained.size(), known.size(), list);
    }
}

} // namespace

uint64 BotMgr::CreateBot(uint8 raceId, uint8 classId, uint8 level, std::string& error)
{
    std::string username;
    uint32 accountId = 0;
    if (!_freeAccounts.empty())
    {
        // an account whose earlier creation never got a character
        std::tie(accountId, username) = _freeAccounts.back();
        _freeAccounts.pop_back();
    }
    else
    {
        AccountOpResult accountResult = AccountOpResult::AOR_NAME_ALREADY_EXIST;
        for (uint32 attempt = 0; attempt < 5 && accountResult == AccountOpResult::AOR_NAME_ALREADY_EXIST; ++attempt)
        {
            username = Trinity::StringFormat("{}{:04}", BOT_ACCOUNT_PREFIX, _nextAccountNumber++);
            std::array<uint8, 8> raw;
            Trinity::Crypto::GetRandomBytes(raw); // a password nobody knows: the account cannot be logged into with a client
            accountResult = sAccountMgr->CreateAccount(username, ByteArrayToHexStr(raw), BOT_ACCOUNT_EMAIL);
        }

        if (accountResult != AccountOpResult::AOR_OK)
        {
            error = Trinity::StringFormat("account {} could not be created (result {})", username, uint32(accountResult));
            return 0;
        }

        accountId = sAccountMgr->GetId(username);
        LoginDatabase.DirectPExecute("UPDATE account SET locked = 1 WHERE id = {}", accountId);
    }

    auto fail = [&](std::string message)
    {
        _freeAccounts.emplace_back(accountId, username);
        error = std::move(message);
        return uint64(0);
    };

    std::unique_ptr<WorldSession> session(MakeBotSession(accountId, username));

    WorldPackets::Character::CharacterCreateInfo createInfo;
    createInfo.Race = raceId;
    createInfo.Class = classId;
    createInfo.Sex = uint8(urand(0, 1));

    for (uint32 attempt = 0; attempt < 200; ++attempt)
    {
        std::string name = GenerateBotName();
        if (ObjectMgr::CheckPlayerName(name, LOCALE_enUS, true) != CHAR_NAME_SUCCESS || sObjectMgr->IsReservedName(name) || sCharacterCache->GetCharacterCacheByName(name))
            continue;

        createInfo.Name = std::move(name);
        break;
    }

    if (createInfo.Name.empty())
        return fail("no free valid bot name found");

    auto deleter = [](Player* player)
    {
        player->CleanupsBeforeDelete();
        delete player;
    };
    std::unique_ptr<Player, decltype(deleter)> player(new Player(session.get()), deleter);
    player->GetMotionMaster()->Initialize();
    if (!player->Create(sObjectMgr->GetGenerator<HighGuid::Player>().Generate(), &createInfo))
        return fail(Trinity::StringFormat("Player::Create refused race {} class {}", raceId, classId));

    // Bot.Dump.Abilities: the first bot of each race and class also logs what a character of that kind has at each level.
    static bool const dumpAbilities = sConfigMgr->GetBoolDefault("Bot.Dump.Abilities", false);
    if (dumpAbilities)
    {
        static std::set<uint32> dumped;
        if (dumped.insert(uint32(raceId) << 8 | classId).second)
            DumpAbilities(session.get(), createInfo);
    }

    // Level up before the first save, so the character is stored at the requested level with its stats and spells.
    if (level > 1)
        player->GiveLevel(level);
    GiveBags(*player);
    if (player->GetClass() == CLASS_HUNTER)
        GiveStarterAmmo(*player);

    player->setCinematic(1);
    player->SetAtLoginFlag(AT_LOGIN_FIRST);

    CharacterDatabaseTransaction characterTransaction = CharacterDatabase.BeginTransaction();
    LoginDatabaseTransaction loginTransaction = LoginDatabase.BeginTransaction();
    player->SaveToDB(loginTransaction, characterTransaction, true);

    LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_REP_REALM_CHARACTERS);
    stmt->setUInt32(0, 1);
    stmt->setUInt32(1, accountId);
    stmt->setUInt32(2, sRealmList->GetCurrentRealmId().Realm);
    loginTransaction->Append(stmt);
    LoginDatabase.CommitTransaction(loginTransaction);

    // Character inserts are async-only; the login waits for FinishCreate so the load never reads a missing row.
    uint64 const guid = player->GetGUID().GetCounter();
    _creating.emplace_back(guid, CharacterDatabase.AsyncCommitTransaction(characterTransaction));
    _creating.back().second.AfterComplete([this, guid](bool success) { FinishCreate(guid, success); });

    sCharacterCache->AddCharacterCacheEntry(player->GetGUID(), accountId, player->GetName(), player->GetNativeGender(), player->GetRace(),
        player->GetClass(), player->GetLevel(), false);
    sScriptMgr->OnPlayerCreate(player.get());

    BotInfo bot;
    bot.Guid = guid;
    bot.AccountId = accountId;
    bot.AccountName = username;
    bot.Name = player->GetName();
    bot.Race = raceId;
    bot.Class = classId;
    bot.Gender = createInfo.Sex;
    bot.Level = player->GetLevel();
    bot.Horde = IsHordeRace(raceId);
    bot.State = BOT_CREATING;
    _bots[guid] = std::move(bot);

    size_t const knownSpells = player->GetSpellMap().size();

    // The spell ids of the first bot of each race and class, once: the level-1 spell list of that combination.
    static std::set<uint32> loggedCombinations;
    if (loggedCombinations.insert(uint32(raceId) << 8 | classId).second)
    {
        std::vector<uint32> spellIds;
        for (auto const& entry : player->GetSpellMap())
            spellIds.push_back(entry.first);
        std::sort(spellIds.begin(), spellIds.end());
        std::string list;
        for (uint32 spellId : spellIds)
            list += (list.empty() ? "" : " ") + std::to_string(spellId);
        TC_LOG_INFO("server.worldserver", "BotMgr: race {} class {} level {} spells ({}): {}", raceId, classId, level, spellIds.size(), list);
    }
    player.reset();
    session.reset();

    TC_LOG_INFO("server.worldserver", "BotMgr: created bot {} (guid {}, race {}, class {}, level {}, {} spells known).", username, guid, raceId, classId, level, knownSpells);
    return guid;
}

// Runs the save callbacks of the characters that finished committing. Nothing else polls them: a bot has no socket
// session, so the usual WorldSession callback queue never sees these.
void BotMgr::PollCreates()
{
    for (size_t i = 0; i < _creating.size();)
    {
        // The callback only records the result; the entry is removed here, after it has returned.
        if (_creating[i].second.InvokeIfReady())
            _creating.erase(_creating.begin() + i);
        else
            ++i;
    }
}

void BotMgr::FinishCreate(uint64 guid, bool success)
{
    auto it = _bots.find(guid);
    if (it == _bots.end())
        return;

    BotInfo& bot = it->second;
    if (!success)
    {
        TC_LOG_ERROR("server.worldserver", "BotMgr: saving new bot {} (guid {}) failed.", bot.Name, guid);
        _freeAccounts.emplace_back(bot.AccountId, bot.AccountName);
        _bots.erase(it);
        return;
    }

    bot.State = BOT_OFFLINE;
    if (bot.LoginRequested)
    {
        bot.LoginRequested = false;
        bot.State = BOT_QUEUED;
        _loginQueue.push_back(guid);
    }
}

bool BotMgr::RequestLogin(uint64 guid, std::string& error)
{
    auto it = _bots.find(guid);
    if (it == _bots.end())
    {
        error = "not a bot";
        return false;
    }

    BotInfo& bot = it->second;
    switch (bot.State)
    {
        case BOT_ONLINE:
        case BOT_LOGGING_IN:
        case BOT_QUEUED:
            return true;
        case BOT_CREATING:
            bot.LoginRequested = true;
            return true;
        case BOT_OFFLINE:
            bot.State = BOT_QUEUED;
            bot.LogoutRequested = false;
            _loginQueue.push_back(guid);
            return true;
    }
    return true;
}

void BotMgr::DeleteBot(uint64 guid)
{
    auto it = _bots.find(guid);
    if (it == _bots.end())
        return;
    if (it->second.Alt)
    {
        StopAlt(guid);
        return;
    }
    RequestLogout(guid, "spawn claim failed");
    Player::DeleteFromDB(ObjectGuid::Create<HighGuid::Player>(guid), it->second.AccountId, true, true);
    _freeAccounts.emplace_back(it->second.AccountId, it->second.AccountName);
    _bots.erase(it);
}

bool BotMgr::RequestLogout(uint64 guid, char const* reason)
{
    auto it = _bots.find(guid);
    if (it == _bots.end())
        return false;

    BotInfo& bot = it->second;
    switch (bot.State)
    {
        case BOT_ONLINE:
            LogoutBot(bot, reason);
            break;
        case BOT_LOGGING_IN:
            bot.LogoutRequested = true; // logs out as soon as it is in the world
            break;
        case BOT_QUEUED:
            _loginQueue.erase(std::remove(_loginQueue.begin(), _loginQueue.end(), guid), _loginQueue.end());
            bot.State = BOT_OFFLINE;
            break;
        case BOT_CREATING:
            bot.LoginRequested = false;
            break;
        case BOT_OFFLINE:
            break;
    }
    return true;
}

void BotMgr::LogoutAll(char const* reason)
{
    std::vector<uint64> guids;
    guids.reserve(_bots.size());
    for (auto const& [guid, bot] : _bots)
        guids.push_back(guid);
    for (uint64 guid : guids)
        RequestLogout(guid, reason);
}

void BotMgr::Update(uint32 diff)
{
    _uptimeMs += diff;
    _lastWorldDiffMs = diff;
    PollCreates();
    PollLogins();
    CheckStopTime();
    ProcessLogins(diff);
    PumpTeleports();
    CollectMail();
}

// What an auction pays a bot arrives as mail (the sale of a listing, an item it bought, a listing that ran out). Nothing opens a mailbox for a bot, so
// it is taken here, for the bots this core made; an alt is a player's own character and keeps its mail for the player.
void BotMgr::CollectMail()
{
    if (!_mailCollect || _uptimeMs - _mailCollectAtMs < _mailCollectEveryMs)
        return;
    _mailCollectAtMs = _uptimeMs;
    for (auto& [guid, bot] : _bots)
        if (bot.State == BOT_ONLINE && bot.Session && !bot.Alt && bot.Session->GetPlayer())
            bot.Session->CollectMailAsBot();
}

// A teleport to another map (or an acknowledged one on the same map) waits for the client: SMSG_SUSPEND_TOKEN is answered by
// CMSG_SUSPEND_TOKEN_RESPONSE, SMSG_NEW_WORLD by CMSG_MOVE_WORLDPORT_ACK, SMSG_MOVE_TELEPORT by CMSG_MOVE_TELEPORT_ACK. A bot has no client
// and its packets are dropped, so without this a bot sent into a dungeon is taken off its old map and never arrives. Each step is
// called here as the session would on receiving the answer, once per world update.
void BotMgr::PumpTeleports()
{
    for (auto& [guid, bot] : _bots)
    {
        if (bot.State != BOT_ONLINE || !bot.Session)
            continue;
        Player* player = bot.Session->GetPlayer();
        if (!player)
            continue;

        if (player->GetTeleportState() == TeleportState::WaitingForSuspendTokenResponse)
        {
            WorldPackets::Movement::SuspendTokenResponse response{ WorldPacket(CMSG_SUSPEND_TOKEN_RESPONSE) };
            bot.Session->HandleSuspendTokenResponse(response);
        }
        if (player->GetTeleportState() == TeleportState::WaitingForWorldPortAck)
            bot.Session->HandleMoveWorldportAck();
        if (player->GetTeleportState() == TeleportState::WaitingForTeleportAck)
        {
            WorldPackets::Movement::MoveTeleportAck ack{ WorldPacket(CMSG_MOVE_TELEPORT_ACK) };
            ack.MoverGUID = player->GetGUID();
            bot.Session->HandleMoveTeleportAck(ack);
        }
    }
}

bool BotMgr::AcceptSpawn(std::string& reason)
{
    std::string refusal;
    if (_stopRequested || (_stopConfigured && _uptimeMs >= _stopAtMs))
        refusal = "the bot run's stop time has passed";
    else if (_bots.size() >= _limitMaxBots)
        refusal = "the bot limit of " + std::to_string(_limitMaxBots) + " is reached";
    else if (_lastWorldDiffMs > _limitMaxWorldDiffMs)
        refusal = "the world update interval is " + std::to_string(_lastWorldDiffMs) + " ms (limit " +
                  std::to_string(_limitMaxWorldDiffMs) + " ms)";
    else if (uint64(CharacterDatabase.QueueSize()) > _limitMaxDbQueue)
        refusal = "the character database queue has " + std::to_string(CharacterDatabase.QueueSize()) + " statements (limit " +
                  std::to_string(_limitMaxDbQueue) + ")";

    if (refusal.empty())
    {
        _lastRefusal.clear();
        return true;
    }

    reason = "not creating a bot: " + refusal;
    if (refusal != _lastRefusal)
    {
        _lastRefusal = refusal;
        TC_LOG_WARN("server.worldserver", "{}.", reason);
    }
    return false;
}

// At the stop time the run ends: a normal shutdown, so the world loop stops and Main.cpp logs every bot out.
void BotMgr::CheckStopTime()
{
    if (!_stopConfigured || _stopRequested || _uptimeMs < _stopAtMs)
        return;

    _stopRequested = true;
    TC_LOG_INFO("server.worldserver", "Bot run stop time reached after {} minutes: no more bots are created, and the server shuts down.",
                _stopAtMs / 60000);
    sWorld->ShutdownServ(60, 0, SHUTDOWN_EXIT_CODE, "bot run stop time");
}

// Advances the logins in flight: a bot that is in the world is finished, one that timed out fails.
void BotMgr::PollLogins()
{
    for (size_t i = 0; i < _loggingIn.size();)
    {
        uint64 const guid = _loggingIn[i];
        auto it = _bots.find(guid);
        if (it == _bots.end() || !it->second.Session)
        {
            _loggingIn.erase(_loggingIn.begin() + i);
            continue;
        }

        BotInfo& bot = it->second;
        bot.Session->ProcessBotLoginCallbacks();
        Player* player = bot.Session->GetPlayer();
        if (player && player->IsInWorld())
        {
            _loggingIn.erase(_loggingIn.begin() + i);
            FinishLogin(bot);
            continue;
        }

        if (_uptimeMs - bot.LoginStartedMs > _loginTimeoutMs)
        {
            _loggingIn.erase(_loggingIn.begin() + i);
            FailLogin(bot, "login timed out");
            continue;
        }
        ++i;
    }
}

void BotMgr::Shutdown(char const* reason)
{
    // The world loop has stopped by now, so Update() does not run again. Finish the logins in flight, waiting at most
    // the login timeout, so each bot is either in the world (and logged out below) or failed. Then log everything out.
    auto const deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(_loginTimeoutMs);
    while (!_loggingIn.empty() && std::chrono::steady_clock::now() < deadline)
    {
        PollLogins();
        if (!_loggingIn.empty())
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::vector<uint64> const stillLoggingIn = _loggingIn;
    for (uint64 guid : stillLoggingIn)
    {
        auto it = _bots.find(guid);
        if (it != _bots.end() && it->second.Session)
            FailLogin(it->second, "server shut down before the login finished");
    }
    _loggingIn.clear();

    LogoutAll(reason);
}

void BotMgr::ProcessLogins(uint32 diff)
{
    // Logins start at Bot.Login.MaxPerSecond a second, at most one second's worth saved up, and at most
    // Bot.Login.MaxPerTick in one tick.
    _loginTokens = std::min<double>(_loginTokens + double(diff) * _loginPerSecond / 1000.0, double(_loginPerSecond));

    uint32 started = 0;
    while (!_loginQueue.empty() && started < _loginMaxPerTick && _loginTokens >= 1.0)
    {
        uint64 const guid = _loginQueue.front();
        auto it = _bots.find(guid);
        if (it == _bots.end() || it->second.State != BOT_QUEUED)
        {
            _loginQueue.pop_front();
            continue;
        }

        BotInfo& bot = it->second;
        if (bot.NotBeforeMs > _uptimeMs)
            break; // the head is still cooling down; keep the order

        _loginQueue.pop_front();
        StartLogin(bot);
        _loginTokens -= 1.0;
        ++started;
    }
}

void BotMgr::StartLogin(BotInfo& bot)
{
    bot.Session = MakeBotSession(bot.AccountId, bot.AccountName);
    bot.State = BOT_LOGGING_IN;
    bot.LoginStartedMs = _uptimeMs;
    bot.Session->BeginBotLogin(ObjectGuid::Create<HighGuid::Player>(bot.Guid));
    _loggingIn.push_back(bot.Guid);
}

// Moves a bot onto its spread point, so bots do not all stand on the race start. Only a bot still on its create spot is
// moved: once it has moved (or been moved), its saved position is kept. A candidate point must have ground at about the
// start's height and a clear line of sight from the start (no wall or ledge in between); otherwise the next candidate is
// tried, and if none fits the bot stays where it is.
void BotMgr::SetStart(uint64 guid, uint32 mapId, float x, float y, float z)
{
    auto itr = _bots.find(guid);
    if (itr == _bots.end())
        return;
    itr->second.HasStart = true;
    itr->second.StartMap = mapId;
    itr->second.StartX = x;
    itr->second.StartY = y;
    itr->second.StartZ = z;
}

void BotMgr::SpreadBot(Player& bot)
{
    if (_spreadSpacing <= 0.0f)
        return;

    PlayerInfo const* info = sObjectMgr->GetPlayerInfo(bot.GetRace(), bot.GetClass());
    if (!info)
        return;

    WorldLocation const& start = info->createPosition.Loc;
    if (start.GetMapId() != bot.GetMapId())
        return; // a bot somewhere else keeps its place
    if (bot.GetExactDist2d(&start) > kCreateSpotYards)
        return; // it has moved since it was created

    uint32 const slot = uint32(bot.GetGUID().GetCounter() % kSpreadSlots);
    float const radius = std::min(_spreadSpacing * std::sqrt(float(slot) + 0.5f), _spreadMaxRadius);
    for (uint32 attempt = 0; attempt < kSpreadAttempts; ++attempt)
    {
        float const turned = radius * (1.0f - 0.1f * float(attempt));
        float const angle = float(slot) * kGoldenAngle + 0.8f * float(attempt);
        float const x = start.GetPositionX() + turned * std::cos(angle);
        float const y = start.GetPositionY() + turned * std::sin(angle);

        float const ground = bot.GetMapHeight(x, y, start.GetPositionZ());
        if (ground <= INVALID_HEIGHT || std::fabs(ground - start.GetPositionZ()) > kSpreadMaxDropYards)
            continue;
        if (!bot.IsWithinLOS(x, y, ground))
            continue;

        bot.TeleportTo(start.GetMapId(), x, y, ground, bot.GetOrientation());
        return;
    }
}

// Spends a bot's unspent talent points (Bot.Learn.Talents). With a profile for the bot's (class, spec) or else (class, role) the points follow it in order, as
// many as the level allows (level minus Bot.Talents.PointsPerLevelOffset, default 9: a guess from the core's comment, one point per
// level from 10). Without a profile the class defaults for the tab are guesses at the usual leveling spec (the tabs are numbered in the
// order of their currencies, which the log line shows); Bot.Talents.Tab.<classId> overrides one. Returns null when it ran, else why not.
static char const* SpendTalents(Player& bot, uint8 role, uint8 spec)
{
    static bool const enabled = sConfigMgr->GetBoolDefault("Bot.Learn.Talents", true);
    if (!enabled)
        return "Bot.Learn.Talents is off";
    if (!bot.IsInWorld() || bot.IsInCombat() || bot.GetLevel() < 10)
        return "the bot is in combat, not in the world or below level 10";

    UF::TraitConfig const* active = bot.GetTraitConfig(bot.m_activePlayerData->ActiveCombatTraitConfigID);
    if (!active)
        return "the bot has no active talent config";

    uint32 const classId = bot.GetClass();
    WorldPackets::Traits::TraitConfig config(*active);
    std::string summary;
    int32 added = 0;

    auto const profile = BotTalentSpec::FindProfile(g_talentProfiles, classId, role, spec);
    if (profile != g_talentProfiles.end())
    {
        static int32 const offset = sConfigMgr->GetIntDefault("Bot.Talents.PointsPerLevelOffset", 9);
        added = TraitMgr::SpendProfile(config, &bot, profile->second, std::max<int32>(0, int32(bot.GetLevel()) - offset), summary);
    }
    else
    {
        static constexpr std::array<uint32, 12> defaultTab = { 0, 0, 2, 0, 1, 2, 0, 0, 2, 0, 0, 1 }; // by class id: warrior Arms, paladin Ret, ...
        uint32 const tab = sConfigMgr->GetIntDefault(("Bot.Talents.Tab." + std::to_string(classId)).c_str(),
            classId < defaultTab.size() ? int32(defaultTab[classId]) : 0);
        added = TraitMgr::AutoSpendClassicTalents(config, &bot, tab, summary);
    }
    if (!added)
        return nullptr;

    bot.UpdateTraitConfig(std::move(config), 0, false);
    TC_LOG_INFO("server.worldserver", "BotMgr: {} (class {}, role {}, spec {}, level {}) spent talents: {}", bot.GetName(), classId, uint32(role), uint32(spec), uint32(bot.GetLevel()), summary);
    return nullptr;
}

// Level-up and login: trainer spells and combat skills, which a bot would otherwise only get by visiting trainers and weapon masters
// and by using its weapons for hours. A trainer's own checks (class, level, required spells) decide what is taught; ranks need the
// previous rank, so the passes repeat until one teaches nothing.
void BotMgr::OnBotLevelChanged(Player& bot)
{
    if (g_dumpingAbilities)
        return;
    if (sBotMgr->IsActiveAlt(bot.GetGUID().GetCounter()))
        return; // a player's own character is not given free trainer spells, skills or talents

    static bool const learnSpells = sConfigMgr->GetBoolDefault("Bot.Learn.TrainerSpells", true);
    static bool const raiseSkills = sConfigMgr->GetBoolDefault("Bot.Learn.CombatSkills", true);

    uint32 taught = learnSpells ? TeachTrainerSpells(bot) : 0;
    taught += BotProfessions::Teach(bot); // the bot's two professions (BotProfessions.h)

    uint32 raised = 0;
    if (raiseSkills)
    {
        // Weapon skills, Unarmed, Thrown, Wands and Defense. They are 5 per level for a player who trained and used them.
        static constexpr std::array<uint32, 17> skills = { 43, 55, 44, 172, 54, 160, 229, 136, 173, 473, 45, 46, 226, 176, 228, 162, 95 };
        uint16 const target = uint16(bot.GetLevel() * 5);
        for (uint32 skill : skills)
        {
            if (!bot.HasSkill(skill))
            {
                SkillRaceClassInfoEntry const* info = sDB2Manager.GetSkillRaceClassInfo(skill, bot.GetRace(), bot.GetClass());
                if (!info)
                    continue;
                bot.LearnDefaultSkill(info); // also learns the proficiency spell
                if (!bot.HasSkill(skill))
                    continue;
            }
            uint16 const wanted = std::max<uint16>(target, bot.GetPureMaxSkillValue(skill));
            if (bot.GetPureSkillValue(skill) < wanted || bot.GetPureMaxSkillValue(skill) < wanted)
            {
                bot.SetSkill(skill, bot.GetSkillStep(skill), wanted, wanted);
                ++raised;
            }
        }
    }

    // With profiles loaded a bot waits for its module to say its role and spec (SetTalentRole): points spent now, before they are known, could
    // not be taken back (no respec yet). After that, each level-up spends with the remembered role and spec.
    if (g_talentProfiles.empty())
        SpendTalents(bot, 0, 0);
    else if (auto const known = g_talentRoles.find(bot.GetGUID().GetCounter()); known != g_talentRoles.end())
        SpendTalents(bot, known->second.first, known->second.second);

    if (taught || raised)
        TC_LOG_INFO("server.worldserver", "BotMgr: {} (level {}) learned {} trainer spell(s), raised {} skill(s).", bot.GetName(),
            uint32(bot.GetLevel()), taught, raised);
}

char const* BotMgr::SetTalentRole(Player& bot, uint8 role, uint8 spec)
{
    uint64 const guid = bot.GetGUID().GetCounter();
    auto const known = g_talentRoles.find(guid);
    if (known != g_talentRoles.end() && (known->second.first != role || known->second.second != spec))
        TC_LOG_INFO("server.worldserver", "BotMgr: {} changed talent role {} spec {} -> role {} spec {}; the points already spent stay (no respec yet)", bot.GetName(),
            uint32(known->second.first), uint32(known->second.second), uint32(role), uint32(spec));
    g_talentRoles[guid] = { role, spec };
    return SpendTalents(bot, role, spec);
}

void BotMgr::FinishLogin(BotInfo& bot)
{
    bot.State = BOT_ONLINE;
    if (Player* player = bot.Session->GetPlayer())
    {
        if (bot.HasStart)
            player->TeleportTo(bot.StartMap, bot.StartX, bot.StartY, bot.StartZ, player->GetOrientation());
        else if (!bot.Alt)
            SpreadBot(*player);
        OnBotLevelChanged(*player);
        BotTaxi::LearnAllNodes(*player);    // the faction's flight points, as a bot must know them to fly
        if (_mailCollect && !bot.Alt)
            bot.Session->CollectMailAsBot(); // mail that came while the bot was away
        if (!bot.Alt)       // a player's own character is not given free items
            if (uint32 bags = GiveBags(*player))
                TC_LOG_INFO("server.worldserver", "BotMgr: bot {} was given {} bag(s).", bot.Name, bags);
        bot.Level = player->GetLevel();
        TC_LOG_INFO("server.worldserver", "BotMgr: bot {} (guid {}) is in the world.", bot.Name, bot.Guid);
    }

    if (bot.LogoutRequested)
    {
        bot.LogoutRequested = false;
        LogoutBot(bot, "LOGOUT_REQUESTED");
    }
}

void BotMgr::FailLogin(BotInfo& bot, char const* reason)
{
    TC_LOG_ERROR("server.worldserver", "BotMgr: login of bot {} (guid {}) failed: {}", bot.Name, bot.Guid, reason);
    delete bot.Session;
    bot.Session = nullptr;
    bot.State = BOT_OFFLINE;
    bot.LogoutRequested = false;
    bot.NotBeforeMs = _uptimeMs + _reloginDelayMs;
}

// Saves and removes the character, drops the session, and tells the controlling module (if any) the bot is gone.
void BotMgr::LogoutBot(BotInfo& bot, char const* reason)
{
    if (!bot.Session)
    {
        bot.State = BOT_OFFLINE;
        return;
    }

    if (Player* player = bot.Session->GetPlayer())
        bot.Level = player->GetLevel();

    sModuleMgr->RevokeBot(bot.Guid, reason);

    bot.Session->LogoutPlayer(true);
    delete bot.Session;
    bot.Session = nullptr;
    bot.State = BOT_OFFLINE;
    bot.NotBeforeMs = _uptimeMs + _reloginDelayMs;

    // Release the vendor-lookup cache entry so the map stays bounded to online bots.
    ClearVendorLookup(bot.Guid);

    TC_LOG_INFO("server.worldserver", "BotMgr: bot {} (guid {}) logged out ({}).", bot.Name, bot.Guid, reason);
}

bool BotMgr::StartAlt(uint64 guid, uint32 accountId, std::string const& accountName, std::string const& name, uint8 race, uint8 classId, uint8 gender,
                     uint8 level, std::string& error)
{
    auto [itr, inserted] = _bots.try_emplace(guid);
    BotInfo& bot = itr->second;
    if (!inserted && (!bot.Alt || bot.State != BOT_OFFLINE))
    {
        error = bot.Alt ? "already logged in as a bot" : "this character is a bot already";
        return false;
    }

    bot.Guid = guid;
    bot.AccountId = accountId;
    bot.AccountName = accountName;
    bot.Name = name;
    bot.Race = race;
    bot.Class = classId;
    bot.Gender = gender;
    bot.Level = level;
    bot.Horde = Player::TeamForRace(race) == HORDE;
    bot.Alt = true;
    bot.LogoutRequested = false;
    bot.LoginRequested = false;
    bot.HasStart = false;
    bot.State = BOT_QUEUED;
    _loginQueue.push_back(guid);
    return true;
}

bool BotMgr::StopAlt(uint64 guid)
{
    auto itr = _bots.find(guid);
    if (itr == _bots.end() || !itr->second.Alt)
        return false;
    return RequestLogout(guid, "LOGOUT_ALT");
}

bool BotMgr::IsActiveAlt(uint64 guid) const
{
    auto itr = _bots.find(guid);
    return itr != _bots.end() && itr->second.Alt && itr->second.State != BOT_OFFLINE;
}

uint32 BotMgr::ActiveAltsOf(uint32 accountId) const
{
    uint32 count = 0;
    for (auto const& [guid, bot] : _bots)
        count += bot.Alt && bot.AccountId == accountId && bot.State != BOT_OFFLINE;
    return count;
}

bool BotMgr::Exists(uint64 guid) const
{
    return _bots.contains(guid);
}

bool BotMgr::GetBot(uint64 guid, BotInfo& out) const
{
    auto it = _bots.find(guid);
    if (it == _bots.end())
        return false;
    out = it->second;
    return true;
}

std::vector<BotInfo> BotMgr::ListBots() const
{
    std::vector<BotInfo> list;
    list.reserve(_bots.size());
    for (auto const& [guid, bot] : _bots)
        list.push_back(bot);
    std::sort(list.begin(), list.end(), [](BotInfo const& a, BotInfo const& b) { return a.Guid < b.Guid; });
    return list;
}

Player* BotMgr::GetOnlinePlayer(uint64 guid) const
{
    auto it = _bots.find(guid);
    if (it == _bots.end() || it->second.State != BOT_ONLINE || !it->second.Session)
        return nullptr;
    return it->second.Session->GetPlayer();
}

uint32 BotMgr::OnlineCount() const
{
    return uint32(std::count_if(_bots.begin(), _bots.end(), [](auto const& entry) { return entry.second.State == BOT_ONLINE; }));
}

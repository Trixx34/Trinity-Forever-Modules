#include "ModuleBotSnapshot.h"
#include "ModuleBotSupplies.h"
#include "ObjectAccessor.h"
#include "Cell.h"
#include "Corpse.h"
#include "GameTime.h"
#include "DB2Structure.h"
#include "CellImpl.h"
#include "CreatureData.h"
#include "Group.h"
#include "DB2Stores.h"
#include "Item.h"
#include "Log.h"
#include "Pet.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "ObjectGuid.h"
#include "ObjectMgr.h"
#include "QuestDef.h"
#include "Player.h"
#include "SpawnData.h"
#include "SharedDefines.h"
#include "Spell.h"
#include "SpellAuraDefines.h"
#include "SpellAuras.h"
#include "SpellHistory.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <unordered_map>
#include <vector>

using forever_module::BotSnapshot;
using forever_module::NearbyUnit;
using forever_module::SpellCooldown;

namespace
{

// How far around a bot the snapshot looks for units, in yards.
constexpr float NearbyRange = 40.0f;

uint32 ClampToUint32(uint64 value)
{
    return static_cast<uint32>(std::min<uint64>(value, std::numeric_limits<uint32>::max()));
}

uint32 ClampToUint32(int64 value)
{
    return value <= 0 ? 0 : ClampToUint32(static_cast<uint64>(value));
}

// The bot's quest log (layout 8): the quest ids in its slots, the status, and the progress of the first objectives.
void FillQuestLog(Player& bot, forever_module::BotSnapshot& entry)
{
    entry.quest_count = 0;
    for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE && entry.quest_count < forever_module::kMaxQuestLog; ++slot)
    {
        uint32 const questId = bot.GetQuestSlotQuestId(slot);
        if (!questId)
            continue;

        forever_module::QuestEntry& out = entry.quests[entry.quest_count];
        out = forever_module::QuestEntry{};
        out.quest_id = questId;
        switch (bot.GetQuestStatus(questId))
        {
            case QUEST_STATUS_COMPLETE: out.status = 2; break;
            case QUEST_STATUS_FAILED: out.status = 3; break;
            default: out.status = 1; break;
        }
        if (Quest const* quest = sObjectMgr->GetQuestTemplate(questId))
        {
            size_t index = 0;
            for (QuestObjective const& objective : quest->Objectives)
            {
                if (index >= forever_module::kMaxQuestObjectives)
                    break;
                out.counts[index++] = uint16(std::clamp<int32>(bot.GetQuestObjectiveData(objective), 0, 65535));
            }
        }
        ++entry.quest_count;
    }
}

// The bot's active cooldowns, closest to expiring first, up to the slots in the snapshot. Each is a known spell
// (not removed) with a remaining cooldown above zero.
void FillCooldowns(Player& bot, SpellCooldown (&slots)[forever_module::kMaxTrackedCooldowns])
{
    struct Active
    {
        uint32 SpellId;
        uint32 Remaining;
    };

    std::vector<Active> active;
    SpellHistory const* history = bot.GetSpellHistory();
    for (auto const& [spellId, spell] : bot.GetSpellMap())
    {
        if (spell.state == PLAYERSPELL_REMOVED)
            continue;
        SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE);
        if (!info)
            continue;

        auto const remaining = history->GetRemainingCooldown(info).count();
        if (remaining <= 0)
            continue;
        active.push_back({ spellId, uint32(std::min<int64>(remaining, std::numeric_limits<uint32>::max())) });
    }

    std::sort(active.begin(), active.end(), [](Active const& a, Active const& b) { return a.Remaining < b.Remaining; });
    for (std::size_t i = 0; i < active.size() && i < forever_module::kMaxTrackedCooldowns; ++i)
        slots[i] = { active[i].SpellId, active[i].Remaining };
}

// A player's unit reference, for a party leader. The leader need not be in the bot's view for the reference to be valid.
uint64 PlayerRef(ObjectGuid guid)
{
    if (guid.GetCounter() > forever_module::kUnitRefCounterMask)
        return 0;
    return forever_module::MakeUnitRef(uint8(HighGuid::Player), 0, guid.GetCounter());
}

// The flags that make a unit impossible for a player to attack, whatever its faction: the ones WorldObject::IsValidAttackTarget
// rejects first. The full function is not used here: its visibility and reputation (at war) checks rejected the creatures the bots
// are meant to fight in the fourth 500-bot sim run, which saw no fights at all.
bool CannotBeAttackedByPlayers(Unit const& unit)
{
    return unit.IsImmuneToPC() || unit.IsUninteractible() ||
           unit.HasUnitFlag(UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_NON_ATTACKABLE_2 | UNIT_FLAG_NOT_ATTACKABLE_1 | UNIT_FLAG_ON_TAXI);
}

// Whether the bot may pick this unit as a target (the snapshot's is_hostile). Hostile units, and neutral creatures ("yellow"): a
// neutral creature can be attacked but does not attack first, and most starter-zone creatures are neutral, so a bot that waited for
// hostile units would never start a fight. Neutral units that are not for fighting are left out: critters, and creatures with an
// NPC role (vendor, trainer, quest giver and the like). Units a player cannot attack are never included.
bool CanBeFought(Player const& bot, Unit const& unit)
{
    if (CannotBeAttackedByPlayers(unit))
        return false;
    if (bot.IsHostileTo(&unit))
        return true;
    if (!unit.IsCreature() || bot.IsFriendlyTo(&unit))
        return false;
    Creature const* creature = unit.ToCreature();
    return creature->GetCreatureTemplate()->type != CREATURE_TYPE_CRITTER && creature->GetNpcFlags() == UNIT_NPC_FLAG_NONE;
}

// Where the vendors of each map stand: the spawn position of every creature whose template has a vendor flag. Built on the
// first call, since the spawn table is loaded at world start. Only the world thread reads it.
struct VendorSpot
{
    float X;
    float Y;
    float Z;
    uint32 Entry;                          // the creature template: the entry field of a unit reference
    uint64 SpawnId;                        // the spawn's low guid: the counter field of a unit reference
    bool Repair;                           // the creature repairs gear (UNIT_NPC_FLAG_REPAIR)
    bool Auctioneer;                       // the creature runs an auction house (UNIT_NPC_FLAG_AUCTIONEER)
    FactionTemplateEntry const* Faction;   // the creature's faction, so a bot does not walk to a town that kills it
    ModuleBotSupplies::VendorStock Stock;  // what it sells that a bot needs (layout 15): the lowest required level of each kind
};

// The NPC behind a layout-6/9 position as a unit reference a module can name in an action (layout 13). It is built from the
// spawn, not from a live creature: a module sends it as BotAction::target_guid, and the core resolves it the same way it
// resolves any other creature reference (ModuleBotActions::ResolveUnit finds it once the creature is loaded on the bot's
// map, which is what an action at that NPC needs anyway).
uint64 VendorSpotRef(VendorSpot const& spot)
{
    if (spot.Entry > forever_module::kUnitRefEntryMask || spot.SpawnId > forever_module::kUnitRefCounterMask)
        return 0;
    return forever_module::MakeUnitRef(uint8(HighGuid::Creature), spot.Entry, spot.SpawnId);
}

std::unordered_map<uint32, std::vector<VendorSpot>> const& VendorsByMap()
{
    static std::unordered_map<uint32, std::vector<VendorSpot>> vendors;
    static bool built = false;
    if (!built)
    {
        built = true;
        auto const started = std::chrono::steady_clock::now();
        size_t spawns = 0;
        std::unordered_map<uint32, ModuleBotSupplies::VendorStock> stockOf; // per creature template: its stock is worked out once
        for (auto const& spawnEntry : sObjectMgr->GetAllCreatureData())
        {
            CreatureData const& spawn = spawnEntry.second;
            ++spawns;
            CreatureTemplate const* templ = sObjectMgr->GetCreatureTemplate(spawn.id);
            if (!templ || (templ->npcflag & (UNIT_NPC_FLAG_VENDOR_MASK | UNIT_NPC_FLAG_REPAIR | UNIT_NPC_FLAG_AUCTIONEER)) == 0)
                continue;
            uint32 const factionId = templ->faction;
            auto stock = stockOf.find(spawn.id);
            if (stock == stockOf.end())
                stock = stockOf.emplace(spawn.id, ModuleBotSupplies::StockOf(spawn.id)).first;
            vendors[spawn.mapId].push_back({ spawn.spawnPoint.GetPositionX(), spawn.spawnPoint.GetPositionY(),
                                             spawn.spawnPoint.GetPositionZ(), spawn.id, spawn.spawnId,
                                             (templ->npcflag & UNIT_NPC_FLAG_REPAIR) != 0,
                                             (templ->npcflag & UNIT_NPC_FLAG_AUCTIONEER) != 0,
                                             sFactionTemplateStore.LookupEntry(factionId), stock->second });
        }

        // One scan of every creature spawn, inside the first snapshot: its cost is logged, since it is a stall of one world update.
        size_t vendorCount = 0;
        for (auto const& onMap : vendors)
            vendorCount += onMap.second.size();
        TC_LOG_INFO("server.worldserver", "Bots: vendor index built from {} creature spawns in {} ms: {} vendors and repairmen on {} maps.",
            spawns, std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count(),
            vendorCount, vendors.size());
    }
    return vendors;
}

// The nearest vendor to a bot on its map, looked up again only when the bot has moved this far from the last lookup.
constexpr float kVendorLookupMoveYards = 100.0f;

// How often the bag counts of the supply facts are redone (milliseconds): counting walks the bags of every bot.
constexpr uint32 kSupplyCountEveryMs = 2000;

struct VendorLookup
{
    bool Made = false;      // a lookup has been made for this bot
    uint32 MapId = 0;
    float X = 0.0f;
    float Y = 0.0f;
    bool FoundVendor = false;   // the map has a vendor friendly to the bot
    VendorSpot Vendor{};
    bool FoundRepair = false;   // ... and a repairman
    VendorSpot Repairman{};
    bool FoundAuctioneer = false;   // ... and an auctioneer
    VendorSpot Auctioneer{};

    // Layout 15: the nearest vendors that sell what the bot can use, chosen for the level, the kind of ranged weapon and the mana the bot
    // has when they were chosen (SupplyMade), and chosen again when any of those changes.
    bool SupplyMade = false;
    uint32 SupplyLevel = 0;
    int32 SupplyAmmoSubclass = -1;
    bool SupplyMana = false;
    bool FoundAmmoVendor = false;
    VendorSpot AmmoVendor{};
    bool FoundFoodVendor = false;
    VendorSpot FoodVendor{};
    // What the bot carries, counted at most every kSupplyCountEveryMs: counting walks the bags.
    uint64 CountedAtMs = 0;
    uint32 AmmoEntry = 0;
    uint32 AmmoCount = 0;
    uint32 FoodCount = 0;
    uint32 DrinkCount = 0;
};

// Process-lifetime cache of the nearest-vendor lookup for each bot. Keyed by character counter. Erased in
// ClearVendorLookup on bot logout so the map never grows beyond the number of currently-online bots.
std::unordered_map<uint64, VendorLookup> g_vendorLookups;

// Layout 15: the nearest vendors, on the bot's map and friendly to it, that sell what it can use. Ammunition: of the kind its ranged weapon
// takes, at a required level the bot has reached. Food: food at such a level, or drink for a bot with mana. Searched from where the
// vendor lookup was made, so it follows the same refresh.
void ChooseSupplyVendors(Player& bot, VendorLookup& last, int32 ammoSubclass, bool mana)
{
    last.SupplyMade = true;
    last.SupplyLevel = bot.GetLevel();
    last.SupplyAmmoSubclass = ammoSubclass;
    last.SupplyMana = mana;
    last.FoundAmmoVendor = false;
    last.FoundFoodVendor = false;

    auto const& vendors = VendorsByMap();
    auto const onMap = vendors.find(last.MapId);
    if (onMap == vendors.end())
        return;

    uint32 const level = bot.GetLevel();
    FactionTemplateEntry const* mine = bot.GetFactionTemplateEntry();
    float bestAmmoSq = std::numeric_limits<float>::max();
    float bestFoodSq = std::numeric_limits<float>::max();
    for (VendorSpot const& spot : onMap->second)
    {
        if (!spot.Stock.SellsAny() || (mine && spot.Faction && mine->IsHostileTo(spot.Faction)))
            continue;
        float const sx = spot.X - last.X;
        float const sy = spot.Y - last.Y;
        float const distSq = sx * sx + sy * sy;

        if (ammoSubclass >= 0 && distSq < bestAmmoSq)
        {
            uint8 const stocked = spot.Stock.MinLevel[ammoSubclass == ITEM_SUBCLASS_BULLET ? ModuleBotSupplies::StockBullets : ModuleBotSupplies::StockArrows];
            if (stocked != ModuleBotSupplies::kNoStock && stocked <= level)
            {
                bestAmmoSq = distSq;
                last.AmmoVendor = spot;
                last.FoundAmmoVendor = true;
            }
        }
        if (distSq < bestFoodSq)
        {
            uint8 const food = spot.Stock.MinLevel[ModuleBotSupplies::StockFood];
            uint8 const drink = spot.Stock.MinLevel[ModuleBotSupplies::StockDrink];
            if ((food != ModuleBotSupplies::kNoStock && food <= level) || (mana && drink != ModuleBotSupplies::kNoStock && drink <= level))
            {
                bestFoodSq = distSq;
                last.FoodVendor = spot;
                last.FoundFoodVendor = true;
            }
        }
    }
}

// Layout 15: what the bot carries of ammunition, food and drink, and where to buy more. Off with Bot.Actions.Supplies.
void FillSupplies(Player& bot, BotSnapshot& entry, VendorLookup& last)
{
    if (!ModuleBotSupplies::Enabled())
        return;

    int32 const ammoSubclass = ModuleBotSupplies::AmmoSubclassOf(bot);
    bool const mana = bot.GetMaxPower(POWER_MANA) > 0;
    if (!last.SupplyMade || last.SupplyLevel != bot.GetLevel() || last.SupplyAmmoSubclass != ammoSubclass || last.SupplyMana != mana)
        ChooseSupplyVendors(bot, last, ammoSubclass, mana);

    uint32 const now = GameTime::GetGameTimeMS();
    if (last.CountedAtMs == 0 || now - last.CountedAtMs >= kSupplyCountEveryMs)
    {
        last.CountedAtMs = now ? now : 1;
        last.AmmoEntry = ModuleBotSupplies::AmmoEntryFor(bot);
        last.AmmoCount = last.AmmoEntry ? bot.GetItemCount(last.AmmoEntry) : 0;
        last.FoodCount = ModuleBotSupplies::CountCarried(bot, ModuleBotSupplies::Kind::Food);
        last.DrinkCount = mana ? ModuleBotSupplies::CountCarried(bot, ModuleBotSupplies::Kind::Drink) : 0;
    }
    entry.ammo_entry = last.AmmoEntry;
    entry.ammo_count = last.AmmoCount;
    entry.food_count = last.FoodCount;
    entry.drink_count = last.DrinkCount;

    if (last.FoundAmmoVendor && ammoSubclass >= 0)
    {
        entry.ammo_vendor_x = last.AmmoVendor.X;
        entry.ammo_vendor_y = last.AmmoVendor.Y;
        entry.ammo_vendor_z = last.AmmoVendor.Z;
        entry.ammo_vendor_valid = 1;
        entry.ammo_vendor_guid = VendorSpotRef(last.AmmoVendor);
    }
    if (last.FoundFoodVendor)
    {
        entry.food_vendor_x = last.FoodVendor.X;
        entry.food_vendor_y = last.FoodVendor.Y;
        entry.food_vendor_z = last.FoodVendor.Z;
        entry.food_vendor_valid = 1;
        entry.food_vendor_guid = VendorSpotRef(last.FoodVendor);
    }
}

// Fills the nearest-vendor fields (layout 6, M6), the nearest-repairman fields (layout 9) and the guids of all three NPCs
// (layout 13: vendor_guid, repair_guid, auctioneer_guid). The position is 2D for the distance:
// vendors sit at different heights in the same town, and the walk is on the ground. Only vendors the bot's faction is not hostile to
// count: a Horde bot must not be sent to Stormwind.
void FillNearestVendor(Player& bot, BotSnapshot& entry)
{
    VendorLookup& last = g_vendorLookups[bot.GetGUID().GetCounter()];

    float const x = bot.GetPositionX();
    float const y = bot.GetPositionY();
    uint32 const mapId = bot.GetMapId();
    float const dx = x - last.X;
    float const dy = y - last.Y;
    bool const fresh = last.Made && last.MapId == mapId && dx * dx + dy * dy <= kVendorLookupMoveYards * kVendorLookupMoveYards;
    if (!fresh)
    {
        last = VendorLookup{};
        last.Made = true;
        last.MapId = mapId;
        last.X = x;
        last.Y = y;
        FactionTemplateEntry const* mine = bot.GetFactionTemplateEntry();
        auto const& vendors = VendorsByMap();
        auto const onMap = vendors.find(mapId);
        if (onMap != vendors.end())
        {
            float bestVendorSq = std::numeric_limits<float>::max();
            float bestRepairSq = std::numeric_limits<float>::max();
            float bestAuctionSq = std::numeric_limits<float>::max();
            for (VendorSpot const& spot : onMap->second)
            {
                if (mine && spot.Faction && mine->IsHostileTo(spot.Faction))
                    continue;
                float const sx = spot.X - x;
                float const sy = spot.Y - y;
                float const distSq = sx * sx + sy * sy;
                if (!spot.Repair && distSq < bestVendorSq)
                {
                    bestVendorSq = distSq;
                    last.Vendor = spot;
                    last.FoundVendor = true;
                }
                if (spot.Repair && distSq < bestRepairSq)
                {
                    bestRepairSq = distSq;
                    last.Repairman = spot;
                    last.FoundRepair = true;
                }
                if (spot.Auctioneer && distSq < bestAuctionSq)
                {
                    bestAuctionSq = distSq;
                    last.Auctioneer = spot;
                    last.FoundAuctioneer = true;
                }
            }
        }
    }

    if (last.FoundVendor)
    {
        entry.vendor_x = last.Vendor.X;
        entry.vendor_y = last.Vendor.Y;
        entry.vendor_z = last.Vendor.Z;
        entry.vendor_valid = 1;
    }
    if (last.FoundRepair)
    {
        entry.repair_x = last.Repairman.X;
        entry.repair_y = last.Repairman.Y;
        entry.repair_z = last.Repairman.Z;
        entry.repair_valid = 1;
        entry.repair_guid = VendorSpotRef(last.Repairman);
    }
    // Layout 13: the NPCs as unit references, so a vendor action can name one instead of the core picking the nearest.
    if (last.FoundVendor)
        entry.vendor_guid = VendorSpotRef(last.Vendor);
    if (last.FoundAuctioneer)
        entry.auctioneer_guid = VendorSpotRef(last.Auctioneer);

    FillSupplies(bot, entry, last);
}

// The lowest durability among the equipped items that wear out, in percent (100 when none does).
uint8 LowestDurabilityPct(Player& bot)
{
    uint32 lowest = 100;
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        Item* item = bot.GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!item)
            continue;
        uint32 const maxDurability = *item->m_itemData->MaxDurability;
        if (!maxDurability)
            continue;
        lowest = std::min<uint32>(lowest, uint32(uint64(*item->m_itemData->Durability) * 100 / maxDurability));
    }
    return uint8(lowest);
}

// The bot's party and any invite it has not answered (layout 5, M8).
void FillParty(Player& bot, BotSnapshot& entry)
{
    if (Group* group = bot.GetGroup())
    {
        entry.group_role = group->IsLeader(bot.GetGUID()) ? 2 : 1;
        entry.group_size = group->GetMembersCount();
        entry.group_leader_guid = PlayerRef(group->GetLeaderGUID());
    }
    if (Group* invite = bot.GetGroupInvite())
        entry.group_invite_from = PlayerRef(invite->GetLeaderGUID());
}

// Death state (layout 11). A ghost has health 1, so a module must test life_state and not health.
void FillLifeState(Player& bot, BotSnapshot& entry)
{
    // The unit whose resurrect offer is pending, as a unit reference a module can answer (a player, or a creature that offers it). Not a reference built
    // from a bare guid: that named a player that does not exist when the offer came from a creature, and the module's answer was refused forever.
    if (ObjectGuid const requester = bot.GetResurrectRequester(); !requester.IsEmpty())
        if (Unit* caster = ObjectAccessor::GetUnit(bot, requester))
        {
            uint64 ref = 0;
            if (EncodeUnitRef(*caster, ref))
                entry.resurrect_from = ref;
        }
    if (bot.IsAlive())
        return;

    bool const ghost = bot.HasPlayerFlag(PLAYER_FLAGS_GHOST);
    entry.life_state = ghost ? 2 : 1;
    if (!ghost)
    {
        // Not released: the corpse is the bot's own body.
        entry.corpse_valid = 1;
        entry.corpse_map = bot.GetMapId();
        entry.corpse_x = bot.GetPositionX();
        entry.corpse_y = bot.GetPositionY();
        entry.corpse_z = bot.GetPositionZ();
        return;
    }

    if (bot.HasCorpse())
    {
        WorldLocation const& where = bot.GetCorpseLocation();
        entry.corpse_valid = 1;
        entry.corpse_map = where.GetMapId();
        entry.corpse_x = where.GetPositionX();
        entry.corpse_y = where.GetPositionY();
        entry.corpse_z = where.GetPositionZ();
    }
    if (Corpse* corpse = bot.GetCorpse())
    {
        int64 const readyAt = int64(corpse->GetGhostTime()) +
            int64(bot.GetCorpseReclaimDelay(corpse->GetType() == CORPSE_RESURRECTABLE_PVP));
        int64 const left = readyAt - int64(GameTime::GetGameTime());
        entry.reclaim_wait_ms = left > 0 ? uint32(std::min<int64>(left * 1000, std::numeric_limits<uint32>::max())) : 0;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Layout 13: auras, pets, guids, money and why the bot lost its target. See doc/module-system/20-layout13-reference.md.
// ---------------------------------------------------------------------------------------------------------------

// AuraEntry::remaining_ms for an aura with no duration (a passive stance, a permanent buff). The module only tests it
// against 0 ("is the aura running?"), so the exact sentinel does not matter as long as it is not 0.
constexpr uint32 InfiniteAuraMs = std::numeric_limits<uint32>::max();

// NearbyUnit::aura_mask. The layout calls the bits "core-defined and fixed per layout" and the SDK does not publish them,
// so this is the core's assignment, frozen with layout 13: a later layout may add bits, but must not move or repurpose
// one. A module that hard-codes these has to check the layout version before it trusts them.
enum AuraMaskBit : uint32
{
    kAuraMaskPeriodicDamage = 0x00000001,   // a DOT: SPELL_AURA_PERIODIC_DAMAGE
    kAuraMaskPeriodicHeal   = 0x00000002,   // SPELL_AURA_PERIODIC_HEAL
    kAuraMaskPoison         = 0x00000004,   // a poison or a disease (Dispel is DISPEL_POISON or DISPEL_DISEASE)
    kAuraMaskCrowdControl   = 0x00000008,   // stun, fear or confusion
    kAuraMaskRoot           = 0x00000010,   // rooted or snared
    kAuraMaskSlow           = 0x00000020,   // SPELL_AURA_MOD_DECREASE_SPEED
    kAuraMaskInvisibility   = 0x00000040,   // SPELL_AURA_MOD_INVISIBILITY
    kAuraMaskAbsorb         = 0x00000080,   // a damage shield or an absorb
};

// What an aura tells a module about its holder: the spell, the time left and the summary bits. A Dot, a poison or a stun is
// what a module acts on, so an aura with bits is preferred over one without when the snapshot has no room for both.
struct AuraFact
{
    uint32 SpellId;
    uint32 RemainingMs;
    uint32 Stacks;
    uint32 Mask;
};

// The summary bits of a spell. Read from the spell's own effects, so it costs the same whatever the aura is of.
uint32 AuraMaskOf(SpellInfo const* info)
{
    if (!info)
        return 0;

    uint32 mask = 0;
    if (info->HasAura(SPELL_AURA_PERIODIC_DAMAGE))
        mask |= kAuraMaskPeriodicDamage;
    if (info->HasAura(SPELL_AURA_PERIODIC_HEAL))
        mask |= kAuraMaskPeriodicHeal;
    if (info->Dispel == DISPEL_POISON || info->Dispel == DISPEL_DISEASE)
        mask |= kAuraMaskPoison;
    if (info->HasAura(SPELL_AURA_MOD_STUN) || info->HasAura(SPELL_AURA_MOD_FEAR) || info->HasAura(SPELL_AURA_MOD_CONFUSE))
        mask |= kAuraMaskCrowdControl;
    if (info->Mechanic == MECHANIC_ROOT || info->Mechanic == MECHANIC_SNARE)
        mask |= kAuraMaskRoot;
    if (info->HasAura(SPELL_AURA_MOD_DECREASE_SPEED))
        mask |= kAuraMaskSlow;
    if (info->HasAura(SPELL_AURA_MOD_INVISIBILITY))
        mask |= kAuraMaskInvisibility;
    if (info->HasAura(SPELL_AURA_SCHOOL_ABSORB) || info->HasAura(SPELL_AURA_DAMAGE_SHIELD))
        mask |= kAuraMaskAbsorb;
    return mask;
}

// The auras a unit carries, at most `capacity` of them, in the order the snapshot reports them. Returns how many were
// written. Two passes over the same container: the auras with summary bits first, so a unit carrying more auras than the
// snapshot has slots for still reports the ones a module acts on. A spell applied by more than one caster is one entry
// with the longest time left (the aura container is keyed by spell id, so a repeat is next to the first). Auras that have
// already run out are left out: an aura the unit does not carry is not "about to expire".
uint32 CollectAuras(Unit const& unit, AuraFact* out, uint32 capacity)
{
    uint32 count = 0;
    for (uint32 pass = 0; pass < 2; ++pass)
    {
        bool const wantMasked = pass == 0;
        for (auto const& [spellId, application] : unit.GetAppliedAuras())
        {
            Aura const* aura = application->GetBase();
            if (!aura)
                continue;

            bool const permanent = aura->IsPermanent();
            int32 const left = aura->GetDuration();
            if (!permanent && left <= 0)
                continue;

            uint32 const mask = AuraMaskOf(aura->GetSpellInfo());
            if (wantMasked != (mask != 0))
                continue;

            uint32 const remaining = permanent ? InfiniteAuraMs : uint32(left);
            uint32 const stacks = std::max<uint32>(1, aura->GetStackAmount());

            // The same spell again (another caster): keep the longer one, and never a second entry for it.
            uint32 slot = count;
            while (slot > 0 && out[slot - 1].SpellId == spellId)
                --slot;
            if (slot != count)
            {
                if (remaining <= out[slot].RemainingMs)
                    continue;
                out[slot].RemainingMs = remaining;
                out[slot].Stacks = std::max(out[slot].Stacks, stacks);
                continue;
            }
            if (count >= capacity)
                continue;

            out[count] = { spellId, remaining, stacks, mask };
            ++count;
        }
    }
    return count;
}

// The bot's own auras (layout 13): the first aura_count entries of auras[]. This replaces what a module had to infer from
// its own timers, so it is filled on every snapshot tick for every bot: the walk is over the aura container the unit
// already keeps, with no lookup of its own, and the entries are written straight into shared memory.
void FillAuras(Player& bot, BotSnapshot& entry)
{
    AuraFact facts[forever_module::kMaxAuras];
    uint32 const count = CollectAuras(bot, facts, forever_module::kMaxAuras);
    entry.aura_count = count;
    for (uint32 i = 0; i < count; ++i)
        entry.auras[i] = { facts[i].SpellId, facts[i].RemainingMs, facts[i].Stacks };
}

// A nearby unit's aura summary (layout 13): how many auras it carries and what they are, coarsely. A nearby unit gets no
// aura array, so only the two numbers are built and nothing is kept -- the walk is the only cost, and the nearby list is
// rebuilt for one bot in refreshEvery ticks (ModuleMgr), not for every bot on every tick.
void FillNearbyAuras(Unit const& unit, NearbyUnit& near)
{
    uint32 count = 0;
    uint32 mask = 0;
    for (auto const& [spellId, application] : unit.GetAppliedAuras())
    {
        (void)spellId;
        Aura const* aura = application->GetBase();
        if (!aura || (!aura->IsPermanent() && aura->GetDuration() <= 0))
            continue;
        ++count;
        mask |= AuraMaskOf(aura->GetSpellInfo());
    }
    near.aura_count = count;
    near.aura_mask = mask;
}

// The bot's own pet (layout 13), as fact instead of what refused casts imply: pet_state 0 none, 1 alive, 2 dead.
void FillPet(Player& bot, BotSnapshot& entry)
{
    Pet* pet = bot.GetPet();
    if (!pet || !pet->IsInWorld())
        return; // pet_state stays 0: the bot has no pet out

    uint64 ref = 0;
    if (EncodeUnitRef(*pet, ref))
        entry.pet_guid = ref;
    entry.pet_health = ClampToUint32(pet->GetHealth());
    entry.pet_max_health = ClampToUint32(pet->GetMaxHealth());
    entry.pet_state = pet->IsAlive() ? 1 : 2;
}

// The bot's money and the room in its bags (layout 13), for a module's buy and hand-in pre-checks. Every inventory slot
// with nothing in it counts: the four backpack slots and every slot of every bag the bot carries. A slot holding a stack
// that is not full is not counted -- the core does not report how much room is in a stack, and a module that needs the
// exact number has the Tier 1 item actions.
void FillMoneyAndBags(Player& bot, BotSnapshot& entry)
{
    entry.copper = bot.GetMoney();

    uint32 freeSlots = 0;
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_BAG_START; ++slot)
        if (!bot.GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            ++freeSlots;
    for (uint8 slot = INVENTORY_SLOT_BAG_START; slot < INVENTORY_SLOT_BAG_END; ++slot)
    {
        Bag* bag = bot.GetBagByPos(slot);
        if (!bag)
            continue;
        for (uint32 i = 0; i < GetBagSize(bag); ++i)
            if (!GetItemInBag(bag, i))
                ++freeSlots;
    }
    entry.bag_free_slots = freeSlots;
}

// Why the bot lost the target of the previous tick (layout 13): 0 none, 1 killed, 2 leashed, 3 despawned, 4 another
// player. A loss the core cannot pin down reports 0, and the module then reads it as a plain target_gone and does not
// avoid the unit -- the one safe direction, since every other code tells a module to leave that unit alone for a while.
// Only checked when the victim actually went away, and kept until the bot picks a new one, so the module reads it on the
// snapshot tick that carries target_gone.
uint32 ClassifyTargetLoss(Player& bot, ObjectGuid const& victim)
{
    if (victim.IsEmpty())
        return 0;

    // A dead creature is a Corpse, not a Unit, so both have to be looked for before anything can be told.
    Unit* lost = ObjectAccessor::GetUnit(bot, victim);
    if (!lost)
    {
        if (ObjectAccessor::GetCorpse(bot, victim))
            return 1; // killed: the corpse is still there
        return 3;    // despawned: nothing of it is in the world any more
    }
    if (!lost->IsAlive())
        return 1; // killed

    // Still standing. Another player took it (the bot is not what it is fighting), or a leashed creature went home.
    if (Unit* itsVictim = lost->GetVictim())
        if (itsVictim != &bot && itsVictim->IsPlayer())
            return 4;
    if (lost->IsCreature())
    {
        Creature const* creature = lost->ToCreature();
        CreatureData const* data = creature->GetCreatureData();
        // Back at its spawn point and out of combat is how a leashed creature resets.
        if (!creature->IsInCombat() && data &&
            creature->GetExactDist2d(data->spawnPoint.GetPositionX(), data->spawnPoint.GetPositionY()) <= 1.0f)
            return 2;
    }
    return 0;
}

} // namespace

bool EncodeUnitRef(Unit const& unit, uint64& ref)
{
    ObjectGuid const guid = unit.GetGUID();
    ObjectGuid::LowType const counter = guid.GetCounter();
    if (counter > forever_module::kUnitRefCounterMask)
        return false;

    switch (guid.GetHigh())
    {
        case HighGuid::Player:
            ref = forever_module::MakeUnitRef(uint8(HighGuid::Player), 0, counter);
            return true;

        // A pet's guid is its own type but packs like a creature's (map, entry, counter), so the same reference shape
        // names either -- see ResolveUnit in ModuleBotActions.cpp, which looks a HighGuid::Pet reference up again.
        case HighGuid::Pet:
        case HighGuid::Creature:
            if (guid.GetEntry() > forever_module::kUnitRefEntryMask)
                return false;
            ref = forever_module::MakeUnitRef(uint8(guid.GetHigh()), guid.GetEntry(), counter);
            return true;

        default:
            return false;
    }
}

ModuleSnapshotBot BuildSnapshotBot(Player& bot, uint32 maxNearby, bool refreshNearby, std::vector<NearbyUnit>& cachedNearby,
                                   BotSnapshotWatch& watch)
{
    ModuleSnapshotBot result;

    BotSnapshot& entry = result.Bot;
    entry.guid = bot.GetGUID().GetCounter();
    entry.pos_x = bot.GetPositionX();
    entry.pos_y = bot.GetPositionY();
    entry.pos_z = bot.GetPositionZ();
    entry.orientation = bot.GetOrientation();
    entry.health = ClampToUint32(bot.GetHealth());
    entry.max_health = ClampToUint32(bot.GetMaxHealth());
    // The main resource of the bot's class: mana, rage or energy (layout 4).
    Powers const powerType = bot.GetPowerType();
    entry.power_type = uint8(powerType);
    entry.power = ClampToUint32(int64(bot.GetPower(powerType)));
    entry.max_power = ClampToUint32(int64(bot.GetMaxPower(powerType)));
    entry.level = bot.GetLevel();
    entry.map_id = bot.GetMapId(); // layout 7: the map, for a module's hubs (M7)

    FillCooldowns(bot, entry.spell_cooldowns);
    FillQuestLog(bot, entry);

    // What the bot is doing now (layout 10): moving, and the cast or channel in progress.
    entry.moving = bot.isMoving() ? 1 : 0;
    for (CurrentSpellTypes type : { CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL })
    {
        if (Spell const* spell = bot.GetCurrentSpell(type))
        {
            if (spell->getState() == SPELL_STATE_FINISHED)
                continue;
            entry.casting_spell = spell->GetSpellInfo()->Id;
            entry.cast_remaining_ms = uint32(std::max<int32>(0, spell->GetTimer()));
            // Layout 13: the unit this cast is aimed at, so a module does not have to guess what a heal or an assist is for.
            // A cast on the bot itself reports 0, which the layout documents as self.
            if (ObjectGuid const castTarget = spell->GetUnitTargetGUID(); !castTarget.IsEmpty() && castTarget != bot.GetGUID())
                if (Unit const* aimed = ObjectAccessor::GetUnit(bot, castTarget))
                    EncodeUnitRef(*aimed, entry.casting_target);
            break;
        }
    }

    entry.in_combat = bot.IsInCombat() ? 1 : 0;
    if (Unit* victim = bot.GetVictim())
    {
        if (!EncodeUnitRef(*victim, entry.threat_target_guid))
            entry.threat_target_guid = 0;
        entry.target_distance = bot.GetDistance(victim);
    }

    FillParty(bot, entry);
    FillLifeState(bot, entry);
    FillNearestVendor(bot, entry);
    entry.durability_pct = LowestDurabilityPct(bot);

    FillAuras(bot, entry);
    FillPet(bot, entry);
    FillMoneyAndBags(bot, entry);

    // Layout 13: why the bot lost the target it had on the previous tick. The reason is worked out once, on the tick the
    // victim goes away, and kept until the bot picks a new one, so the module reads it on the tick that carries
    // threat_target_guid == 0. A bot that still has its victim clears it.
    if (Unit* victim = bot.GetVictim())
    {
        watch.LastVictim = victim->GetGUID();
        watch.LostReason = 0;
    }
    else if (!watch.LastVictim.IsEmpty())
    {
        watch.LostReason = ClassifyTargetLoss(bot, watch.LastVictim);
        watch.LastVictim = ObjectGuid::Empty;
    }
    entry.last_target_lost_reason = watch.LostReason;

    if (maxNearby == 0)
        return result;

    // Between refreshes the bot's own entry is current, and its nearby units are the ones from the last refresh.
    if (!refreshNearby && !cachedNearby.empty())
    {
        result.Nearby = cachedNearby;
        if (result.Nearby.size() > maxNearby)
            result.Nearby.resize(maxNearby);
        return result;
    }

    // Every unit in range, split into players and creatures, each closest first. Dead creatures are kept (a boss's corpse is how a
    // scenario sees the boss die); dead players are not.
    std::list<Unit*> inRange;
    Trinity::AnyUnitInObjectRangeCheck check(&bot, NearbyRange, true, false);
    Trinity::UnitListSearcher<Trinity::AnyUnitInObjectRangeCheck> searcher(&bot, inRange, check);
    Cell::VisitAllObjects(&bot, searcher, NearbyRange);

    std::vector<Unit*> players;
    std::vector<Unit*> creatures;
    std::vector<Unit*> corpses;
    for (Unit* unit : inRange)
    {
        if (unit == &bot)
            continue;
        if (unit->IsPlayer())
        {
            if (unit->IsAlive())
                players.push_back(unit);
        }
        else
            (unit->IsAlive() ? creatures : corpses).push_back(unit);
    }
    auto closerFirst = [&bot](Unit* a, Unit* b)
    {
        return bot.GetExactDist2dSq(a) < bot.GetExactDist2dSq(b);
    };
    std::sort(players.begin(), players.end(), closerFirst);
    std::sort(creatures.begin(), creatures.end(), closerFirst);
    std::sort(corpses.begin(), corpses.end(), closerFirst);

    // The closest few players, then living creatures, then corpses, then any players that are left if there is still room. Corpses
    // come after the living so that they never take a slot a live target needs.
    std::vector<Unit*> chosen;
    size_t const playerSlots = std::min<size_t>(kNearbyPlayerSlots, maxNearby);
    for (Unit* player : players)
    {
        if (chosen.size() >= playerSlots)
            break;
        chosen.push_back(player);
    }
    size_t const leadingPlayers = chosen.size();
    for (Unit* creature : creatures)
    {
        if (chosen.size() >= maxNearby)
            break;
        chosen.push_back(creature);
    }
    for (Unit* corpse : corpses)
    {
        if (chosen.size() >= maxNearby)
            break;
        chosen.push_back(corpse);
    }
    for (size_t i = leadingPlayers; i < players.size() && chosen.size() < maxNearby; ++i)
        chosen.push_back(players[i]);
    std::sort(chosen.begin(), chosen.end(), closerFirst);

    for (Unit* unit : chosen)
    {
        NearbyUnit near;
        if (!EncodeUnitRef(*unit, near.guid))
            continue;
        near.pos_x = unit->GetPositionX();
        near.pos_y = unit->GetPositionY();
        near.pos_z = unit->GetPositionZ();
        near.health = ClampToUint32(unit->GetHealth());
        near.max_health = ClampToUint32(unit->GetMaxHealth());
        // is_hostile means the bot may fight it: hostile by faction or a neutral creature, and not one a player cannot attack (a
        // trainer immune to player characters is hostile by faction, but a bot that picked it would never land a hit).
        near.is_hostile = CanBeFought(bot, *unit);
        near.level = unit->GetLevel();
        near.elite = unit->IsCreature() && unit->ToCreature()->IsElite() ? 1 : 0;
        near.casting = unit->IsNonMeleeSpellCast(false) ? 1 : 0;
        FillNearbyAuras(*unit, near); // layout 13
        result.Nearby.push_back(near);
    }

    cachedNearby = result.Nearby;
    return result;
}

bool BotMayFight(Player const& bot, Unit const& unit)
{
    return CanBeFought(bot, unit);
}

// Removes the vendor-lookup cache entry for a bot that has logged out. The static map is keyed by character
// counter and never erased otherwise, so without this call it would grow to one entry per ever-created bot.
// Called by BotMgr::LogoutBot after the session has been torn down.
void ClearVendorLookup(uint64 botCounter)
{
    g_vendorLookups.erase(botCounter);
}

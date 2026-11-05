/*
 * Spell rank groups. See BotSpellRanks.h and BotSpellRankRules.h.
 */

#include "BotSpellRanks.h"
#include "BotSpellRankRules.h"
#include "DB2Stores.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Trainer.h"

#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace
{

BotSpellRankRules::RankGroups g_rankGroups;

// The grouping row of a spell, or false when the spell has a native rank chain (SpellMgr::LoadSpellRanks; that chain is used as it is), no class
// family or no English name. level 0: the spell's own level (SpellLevels: SpellLevel, else BaseLevel).
bool MakeRow(uint32 spellId, uint32 level, bool trained, BotSpellRankRules::SpellRow& row)
{
    SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE);
    if (!info || !info->SpellFamilyName || !info->SpellName || sSpellMgr->GetSpellChainNode(spellId))
        return false;
    row.Id = spellId;
    row.Family = info->SpellFamilyName;
    row.Icon = info->IconFileDataId;
    row.Name = (*info->SpellName)[LOCALE_enUS];
    row.Level = level ? level : (info->SpellLevel ? info->SpellLevel : info->BaseLevel);
    row.Trained = trained;
    return !row.Name.empty();
}

} // namespace

void BotSpellRanks::Build()
{
    std::vector<BotSpellRankRules::SpellRow> rows;
    std::set<std::tuple<uint32, uint32, std::string>> trainedKeys;

    // The class trainers' spells (the trainers TeachTrainerSpells in BotMgr.cpp uses: no profession or pet trainers), at the trainer row's level.
    for (auto const& [trainerId, trainer] : sObjectMgr->GetTrainers())
    {
        if (trainer.GetType() == Trainer::Type::Tradeskill || trainer.GetType() == Trainer::Type::Pet)
            continue;
        for (Trainer::Spell const& trainerSpell : trainer.Spells())
        {
            // A trainer row is the spell itself, or a spell that teaches it (SPELL_EFFECT_LEARN_SPELL, Trainer::Spell::IsCastable).
            std::vector<uint32> learned;
            if (SpellInfo const* teach = sSpellMgr->GetSpellInfo(trainerSpell.SpellId, DIFFICULTY_NONE))
                for (SpellEffectInfo const& effect : teach->GetEffects())
                    if (effect.IsEffect(SPELL_EFFECT_LEARN_SPELL) && effect.TriggerSpell)
                        learned.push_back(effect.TriggerSpell);
            if (learned.empty())
                learned.push_back(trainerSpell.SpellId);

            for (uint32 spellId : learned)
            {
                BotSpellRankRules::SpellRow row;
                if (!MakeRow(spellId, trainerSpell.ReqLevel, true, row))
                    continue;
                trainedKeys.insert(std::make_tuple(row.Family, row.Icon, row.Name));
                rows.push_back(std::move(row));
            }
        }
    }

    // Spells no trainer teaches (rank 1 of many spells is known from character creation) join the group of a trainer spell with the same family,
    // icon and name. Only player abilities (SkillLineAbility) are looked at, so the NPC copies of a spell (Fireball 9053 ...) stay out.
    for (SkillLineAbilityEntry const* ability : sSkillLineAbilityStore)
    {
        if (ability->Spell <= 0)
            continue;
        BotSpellRankRules::SpellRow row;
        if (MakeRow(uint32(ability->Spell), 0, false, row) && trainedKeys.count(std::make_tuple(row.Family, row.Icon, row.Name)))
            rows.push_back(std::move(row));
    }

    g_rankGroups = BotSpellRankRules::BuildRankGroups(rows);

    for (BotSpellRankRules::RankGroup const& group : g_rankGroups.Skipped)
    {
        std::string members;
        for (BotSpellRankRules::Member const& member : group.Members)
            members += (members.empty() ? "" : ", ") + std::to_string(member.Id) + " (level " + std::to_string(member.Level) + ")";
        TC_LOG_INFO("server.worldserver", "BotSpellRanks: '{}' (family {}, icon {}) left without ranks: two of its spells share a level: {}",
            group.Name, group.Family, group.Icon, members);
    }
    TC_LOG_INFO("server.worldserver", "BotSpellRanks: {} rank group(s) of {} spell(s) for spells without a rank chain, {} group(s) left out",
        g_rankGroups.Groups.size(), g_rankGroups.GroupOf.size(), g_rankGroups.Skipped.size());
}

uint32 BotSpellRanks::HighestKnownInGroup(Player const& bot, uint32 spellId)
{
    return BotSpellRankRules::HighestKnownGroupRank(g_rankGroups, spellId, uint32(bot.GetLevel()), [&bot](uint32 id) { return bot.HasSpell(id); });
}

uint32 BotSpellRanks::NextRank(uint32 spellId)
{
    return BotSpellRankRules::NextGroupRank(g_rankGroups, spellId);
}

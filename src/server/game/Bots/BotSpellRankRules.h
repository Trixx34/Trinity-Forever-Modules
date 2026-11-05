/*
 * Pure rules of the spell rank groups (see BotSpellRanks.h). The client data of this build has a rank chain (SkillLineAbility.SupercedesSpell,
 * SpellMgr::LoadSpellRanks) for few Classic spells: Fireball, Smite, Heal, Frostbolt ... have none, so a walk of the chain from rank 1 ends at rank 1.
 * For those spells the ranks are grouped by (spell family, icon, English name) and ordered by the level they are learned at. Only numbers and
 * strings, no spell, player or world objects, so the rules can be tested alone (tests/game/BotSpellRankRules.cpp).
 */

#ifndef TRINITY_BOT_SPELL_RANK_RULES_H
#define TRINITY_BOT_SPELL_RANK_RULES_H

#include "Define.h"
#include <algorithm>
#include <cstddef>
#include <map>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace BotSpellRankRules
{
// One spell for the grouping. Family 0 (no class family) and an empty name are never grouped. Trained: a class trainer teaches it; a group needs
// at least one trained member (rank 1 of many spells is known from character creation and comes from SkillLineAbility only).
struct SpellRow
{
    uint32 Id = 0;
    uint32 Family = 0;
    uint32 Icon = 0;
    std::string Name;
    uint32 Level = 0;
    bool Trained = false;
};

struct Member
{
    uint32 Id = 0;
    uint32 Level = 0;
};

struct RankGroup
{
    uint32 Family = 0;
    uint32 Icon = 0;
    std::string Name;
    std::vector<Member> Members; // by level, then by id
};

struct RankGroups
{
    std::vector<RankGroup> Groups;                    // the groups in use: two or more members, each at its own level
    std::vector<RankGroup> Skipped;                   // groups left alone because two members share a level (talent or racial look-alikes)
    std::unordered_map<uint32, std::size_t> GroupOf;  // spell id -> index into Groups
};

// Groups the rows by (family, icon, name). A spell id listed twice (two trainers, or a trainer row and a SkillLineAbility row) counts once, at the
// lower of its levels. Groups of one member and groups with no trained member are dropped; groups with two members at one level go to Skipped.
inline RankGroups BuildRankGroups(std::vector<SpellRow> const& rows)
{
    struct Pending
    {
        std::map<uint32, Member> ById;
        bool Trained = false;
    };
    std::map<std::tuple<uint32, uint32, std::string>, Pending> pending;
    for (SpellRow const& row : rows)
    {
        if (!row.Id || !row.Family || row.Name.empty())
            continue;
        Pending& group = pending[std::make_tuple(row.Family, row.Icon, row.Name)];
        group.Trained = group.Trained || row.Trained;
        auto const inserted = group.ById.try_emplace(row.Id, Member{ row.Id, row.Level });
        if (!inserted.second)
            inserted.first->second.Level = std::min(inserted.first->second.Level, row.Level);
    }

    RankGroups result;
    for (auto const& entry : pending)
    {
        Pending const& found = entry.second;
        if (!found.Trained || found.ById.size() < 2)
            continue;
        RankGroup group;
        group.Family = std::get<0>(entry.first);
        group.Icon = std::get<1>(entry.first);
        group.Name = std::get<2>(entry.first);
        for (auto const& member : found.ById)
            group.Members.push_back(member.second);
        // ById is ordered by id, so a stable sort by level leaves equal levels by id
        std::stable_sort(group.Members.begin(), group.Members.end(), [](Member const& a, Member const& b) { return a.Level < b.Level; });
        bool const sharedLevel = std::adjacent_find(group.Members.begin(), group.Members.end(),
            [](Member const& a, Member const& b) { return a.Level == b.Level; }) != group.Members.end();
        if (sharedLevel)
        {
            result.Skipped.push_back(std::move(group));
            continue;
        }
        std::size_t const index = result.Groups.size();
        for (Member const& member : group.Members)
            result.GroupOf[member.Id] = index;
        result.Groups.push_back(std::move(group));
    }
    return result;
}

// The highest rank at or above spellId in its group that the bot knows (knows(id)) and whose level is at most botLevel. 0 when spellId is in no
// group or no such rank is known. Like the chain walk it only looks upward: a bot that knows only a lower rank than spellId gets 0.
template<typename Knows>
uint32 HighestKnownGroupRank(RankGroups const& groups, uint32 spellId, uint32 botLevel, Knows&& knows)
{
    auto const found = groups.GroupOf.find(spellId);
    if (found == groups.GroupOf.end())
        return 0;
    uint32 best = 0;
    bool reached = false;
    for (Member const& member : groups.Groups[found->second].Members)
    {
        reached = reached || member.Id == spellId;
        if (reached && member.Level <= botLevel && knows(member.Id))
            best = member.Id;
    }
    return best;
}

// The rank after spellId in its group; 0 for the last rank or a spell in no group.
inline uint32 NextGroupRank(RankGroups const& groups, uint32 spellId)
{
    auto const found = groups.GroupOf.find(spellId);
    if (found == groups.GroupOf.end())
        return 0;
    std::vector<Member> const& members = groups.Groups[found->second].Members;
    for (std::size_t i = 0; i + 1 < members.size(); ++i)
        if (members[i].Id == spellId)
            return members[i + 1].Id;
    return 0;
}
} // namespace BotSpellRankRules

#endif // TRINITY_BOT_SPELL_RANK_RULES_H

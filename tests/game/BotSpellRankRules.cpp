#include "tc_catch2.h"
#include "BotSpellRankRules.h"

#include <set>
#include <string>
#include <vector>

using namespace BotSpellRankRules;

namespace
{
constexpr uint32 kMage = 3, kPriest = 6;
constexpr uint32 kFireballIcon = 135812, kHealIcon = 135913, kLookAlikeIcon = 136000;

// Fireball ranks 1 to 4 as the client data has them: no rank chain; rank 1 comes from SkillLineAbility only (known from character creation), the
// others from the mage trainer. The NPC copies of Fireball are not in the rows (BotSpellRanks::Build only takes player abilities).
std::vector<SpellRow> FireballRows()
{
    return {
        { 8400, kMage, kFireballIcon, "Fireball", 18, true },
        { 133, kMage, kFireballIcon, "Fireball", 1, false },
        { 145, kMage, kFireballIcon, "Fireball", 12, true },
        { 143, kMage, kFireballIcon, "Fireball", 6, true },
    };
}

// A predicate for "the bot knows this spell".
auto Knows(std::set<uint32> known)
{
    return [known](uint32 id) { return known.count(id) > 0; };
}
}

TEST_CASE("Fireball ranks 1 to 4 form one group ordered by learn level", "[BotSpellRankRules]")
{
    RankGroups const groups = BuildRankGroups(FireballRows());
    REQUIRE(groups.Groups.size() == 1);
    CHECK(groups.Skipped.empty());
    std::vector<uint32> ids;
    for (Member const& member : groups.Groups[0].Members)
        ids.push_back(member.Id);
    std::vector<uint32> const expected = { 133, 143, 145, 8400 };
    CHECK(ids == expected);

    CHECK(NextGroupRank(groups, 133) == 143);
    CHECK(NextGroupRank(groups, 143) == 145);
    CHECK(NextGroupRank(groups, 145) == 8400);
    CHECK(NextGroupRank(groups, 8400) == 0);

    // a level-23 mage who learned every rank casts rank 4 when the module names rank 1
    CHECK(HighestKnownGroupRank(groups, 133, 23, Knows({ 133, 143, 145, 8400 })) == 8400);
    // and from any rank it names, never a lower one
    CHECK(HighestKnownGroupRank(groups, 145, 23, Knows({ 133, 143, 145, 8400 })) == 8400);
}

TEST_CASE("The bot's level caps the rank", "[BotSpellRankRules]")
{
    RankGroups const groups = BuildRankGroups(FireballRows());
    auto const knowsAll = Knows({ 133, 143, 145, 8400 });
    CHECK(HighestKnownGroupRank(groups, 133, 1, knowsAll) == 133);
    CHECK(HighestKnownGroupRank(groups, 133, 6, knowsAll) == 143);
    CHECK(HighestKnownGroupRank(groups, 133, 11, knowsAll) == 143);
    CHECK(HighestKnownGroupRank(groups, 133, 12, knowsAll) == 145);
    CHECK(HighestKnownGroupRank(groups, 133, 17, knowsAll) == 145);
    CHECK(HighestKnownGroupRank(groups, 133, 18, knowsAll) == 8400);
}

TEST_CASE("Only ranks the bot knows are cast", "[BotSpellRankRules]")
{
    RankGroups const groups = BuildRankGroups(FireballRows());
    CHECK(HighestKnownGroupRank(groups, 133, 23, Knows({ 133 })) == 133);
    CHECK(HighestKnownGroupRank(groups, 133, 23, Knows({ 133, 143 })) == 143);
    // a gap: rank 3 not learned, rank 4 learned
    CHECK(HighestKnownGroupRank(groups, 133, 23, Knows({ 133, 143, 8400 })) == 8400);
    // knows no rank at or above the one named: 0, as the chain walk gives
    CHECK(HighestKnownGroupRank(groups, 133, 23, Knows({})) == 0);
    CHECK(HighestKnownGroupRank(groups, 145, 23, Knows({ 133, 143 })) == 0);
}

TEST_CASE("A group with two spells at one level is left alone", "[BotSpellRankRules]")
{
    std::vector<SpellRow> rows = FireballRows();
    // a talent or racial look-alike: same family, icon and name, same level as another member
    rows.push_back({ 2054, kPriest, kLookAlikeIcon, "Heal", 16, true });
    rows.push_back({ 2055, kPriest, kLookAlikeIcon, "Heal", 22, true });
    rows.push_back({ 9999, kPriest, kLookAlikeIcon, "Heal", 22, true });
    RankGroups const groups = BuildRankGroups(rows);

    REQUIRE(groups.Skipped.size() == 1);
    CHECK(groups.Skipped[0].Name == "Heal");
    CHECK(groups.Skipped[0].Members.size() == 3);
    REQUIRE(groups.Groups.size() == 1); // Fireball is still grouped
    CHECK(groups.Groups[0].Name == "Fireball");

    for (uint32 id : { 2054u, 2055u, 9999u })
    {
        CHECK(NextGroupRank(groups, id) == 0);
        CHECK(HighestKnownGroupRank(groups, id, 60, Knows({ 2054, 2055, 9999 })) == 0);
    }
}

TEST_CASE("A spell with a native rank chain is not grouped", "[BotSpellRankRules]")
{
    // BotSpellRanks::Build leaves spells with a SpellMgr chain out of the rows (MakeRow), so a chained spell is never in a group and
    // HighestKnownRank keeps the chain's answer. Here: Backstab ranks are not in the rows, only Fireball is.
    RankGroups const groups = BuildRankGroups(FireballRows());
    constexpr uint32 kBackstab1 = 53, kBackstab2 = 2589;
    CHECK(groups.GroupOf.count(kBackstab1) == 0);
    CHECK(NextGroupRank(groups, kBackstab1) == 0);
    CHECK(HighestKnownGroupRank(groups, kBackstab1, 60, Knows({ kBackstab1, kBackstab2 })) == 0);
}

TEST_CASE("Rows that cannot be told apart from other spells are not grouped", "[BotSpellRankRules]")
{
    std::vector<SpellRow> const rows = {
        // no trainer teaches any of them: not a class spell group
        { 9053, kMage, kFireballIcon, "Fireball", 20, false },
        { 9487, kMage, kFireballIcon, "Fireball", 5, false },
        // no class family
        { 10, 0, 1, "Shoot", 1, true },
        { 11, 0, 1, "Shoot", 5, true },
        // one rank only
        { 2054, kPriest, kHealIcon, "Heal", 16, true },
    };
    RankGroups const groups = BuildRankGroups(rows);
    CHECK(groups.Groups.empty());
    CHECK(groups.Skipped.empty());
    CHECK(groups.GroupOf.empty());
}

TEST_CASE("A spell listed twice counts once, at its lower level", "[BotSpellRankRules]")
{
    std::vector<SpellRow> rows = FireballRows();
    rows.push_back({ 143, kMage, kFireballIcon, "Fireball", 8, false }); // a second row for rank 2 (SkillLineAbility), at another level
    rows.push_back({ 143, kMage, kFireballIcon, "Fireball", 6, true });  // the same trainer row from a second trainer
    RankGroups const groups = BuildRankGroups(rows);
    REQUIRE(groups.Groups.size() == 1);
    REQUIRE(groups.Groups[0].Members.size() == 4);
    CHECK(groups.Groups[0].Members[1].Id == 143);
    CHECK(groups.Groups[0].Members[1].Level == 6);
}

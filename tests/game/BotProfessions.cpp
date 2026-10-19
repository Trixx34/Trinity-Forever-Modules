#include "tc_catch2.h"
#include "BotProfessions.h"

#include <set>

using namespace BotProfessions;

TEST_CASE("Two professions go together only as the owner's rules allow", "[BotProfessions]")
{
    // any two gathering professions, and the five crafting pairs: 8 unordered pairs, 16 in order
    CHECK(IsAllowedPair(SKILL_MINING, SKILL_HERBALISM));
    CHECK(IsAllowedPair(SKILL_HERBALISM, SKILL_SKINNING));
    CHECK(IsAllowedPair(SKILL_ENGINEERING, SKILL_MINING));
    CHECK(IsAllowedPair(SKILL_ALCHEMY, SKILL_HERBALISM));
    CHECK(IsAllowedPair(SKILL_ENCHANTING, SKILL_TAILORING));
    CHECK_FALSE(IsAllowedPair(SKILL_ALCHEMY, SKILL_BLACKSMITHING));
    CHECK_FALSE(IsAllowedPair(SKILL_ENGINEERING, SKILL_TAILORING));
    CHECK_FALSE(IsAllowedPair(SKILL_HERBALISM, SKILL_LEATHERWORKING));
    CHECK_FALSE(IsAllowedPair(SKILL_MINING, SKILL_MINING));

    uint32 ordered = 0;
    for (uint32 a : kPrimary)
        for (uint32 b : kPrimary)
        {
            CHECK(IsAllowedPair(a, b) == IsAllowedPair(b, a));
            if (IsAllowedPair(a, b))
                ++ordered;
        }
    CHECK(ordered == 16);
}

TEST_CASE("Secondary professions are never restricted", "[BotProfessions]")
{
    for (uint32 skill : { uint32(SKILL_COOKING), uint32(129) /* First Aid */, uint32(SKILL_FISHING), uint32(SKILL_RIDING), uint32(0) })
        for (uint64 guid = 1; guid <= 50; ++guid)
            CHECK(MayLearn(guid, skill));
}

TEST_CASE("Every bot gets one of the eight sets, always the same one", "[BotProfessions]")
{
    std::set<std::array<uint32, 2>> seen;
    for (uint64 guid = 1; guid <= 2000; ++guid)
    {
        std::array<uint32, 2> const set = SetFor(guid);
        CHECK(IsAllowedPair(set[0], set[1]));
        CHECK(SetFor(guid) == set);
        seen.insert(set);
    }
    CHECK(seen.size() == kSets.size());
}

TEST_CASE("A bot may learn exactly its own two primary professions", "[BotProfessions]")
{
    for (uint64 guid = 1; guid <= 500; ++guid)
    {
        std::array<uint32, 2> const set = SetFor(guid);
        uint32 allowed = 0;
        for (uint32 skill : kPrimary)
        {
            bool const own = skill == set[0] || skill == set[1];
            CHECK(MayLearn(guid, skill) == own);
            CHECK(IsOneOf(guid, skill) == own);
            if (MayLearn(guid, skill))
                ++allowed;
        }
        CHECK(allowed == 2);
    }
}

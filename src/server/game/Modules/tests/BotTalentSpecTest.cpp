/*
 * Unit test of Bots/BotTalentSpec.h: the spec table, the profile key words and the lookup order (spec profile, then role profile, then none).
 * Not part of the worldserver build. Build and run it from the core root:
 *   g++ -std=c++17 -Isrc/server/game/Bots src/server/game/Modules/tests/BotTalentSpecTest.cpp -o bot_talent_spec_test && ./bot_talent_spec_test
 */

#include "BotTalentSpec.h"

#include <cstdio>
#include <map>
#include <string>
#include <utility>

namespace
{

int g_failures = 0;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (!(cond)) {                                                              \
            ++g_failures;                                                           \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
        }                                                                           \
    } while (0)

using namespace BotTalentSpec;
using Profiles = std::map<std::pair<std::uint32_t, std::uint8_t>, int>;

} // namespace

int main()
{
    // The table: ids 1 to 28 in order, the nine classes, a key unique inside its class, lookups agree.
    {
        CHECK(Specs.size() == 28 && LastSpec == 28);
        CHECK(Get(0) == nullptr && Get(29) == nullptr);
        std::map<std::uint32_t, int> perClass;
        for (std::uint32_t id = 1; id <= LastSpec; ++id)
        {
            Info const* info = Get(id);
            CHECK(info && info->Spec == id);
            if (!info)
                continue;
            ++perClass[info->ClassId];
            CHECK(FromKey(info->ClassId, info->Key) == id);
            for (std::uint32_t other = id + 1; other <= LastSpec; ++other)
                CHECK(!(Get(other)->ClassId == info->ClassId && std::string_view(Get(other)->Key) == info->Key));
        }
        CHECK(perClass.size() == 9 && perClass[1] == 3 && perClass[2] == 3 && perClass[3] == 3 && perClass[4] == 3 && perClass[5] == 3 &&
              perClass[7] == 3 && perClass[8] == 3 && perClass[9] == 3 && perClass[11] == 4);
        CHECK(FromKey(3, "bm") == 7 && FromKey(3, "mm") == 8 && FromKey(3, "sv") == 9);
        CHECK(FromKey(1, "prot") == 3 && FromKey(2, "prot") == 5 && FromKey(3, "prot") == 0);
        CHECK(FromKey(11, "feral_cat") == 27 && FromKey(11, "feral_tank") == 26 && FromKey(11, "balance") == 25 && FromKey(11, "resto") == 28);
        CHECK(FromKey(7, "resto") == 18 && FromKey(5, "holy") == 14 && FromKey(2, "holy") == 4);
        CHECK(FromKey(3, "dps") == 0 && FromKey(3, "") == 0 && FromKey(6, "bm") == 0);
    }

    // Profile keys: the three role words, the class's spec keys (101 to 128), nothing else.
    {
        CHECK(ProfileKeyFromName(3, "tank") == 1 && ProfileKeyFromName(3, "healer") == 2 && ProfileKeyFromName(3, "dps") == 3);
        CHECK(ProfileKeyFromName(3, "bm") == ProfileKeyOfSpec(7) && ProfileKeyOfSpec(7) == 107);
        CHECK(ProfileKeyFromName(11, "feral_cat") == 127 && ProfileKeyFromName(11, "resto") == 128);
        CHECK(ProfileKeyFromName(3, "prot") == 0);        // a warrior key on a hunter line is rejected
        CHECK(ProfileKeyFromName(3, "DPS") == 0);         // lowercase words only
        CHECK(ProfileKeyFromName(3, "bm,mm") == 0);
        CHECK(std::string(ProfileKeyName(1)) == "tank" && std::string(ProfileKeyName(3)) == "dps");
        CHECK(std::string(ProfileKeyName(ProfileKeyOfSpec(27))) == "feral_cat" && std::string(ProfileKeyName(0)) == "none");
        CHECK(std::string(ProfileKeyName(4)) == "none" && std::string(ProfileKeyName(129)) == "none");
    }

    // The lookup: (class, spec) first, then (class, role), else none. A role or spec of 0 skips that lookup.
    {
        Profiles profiles;
        profiles[{ 3, RoleDps }] = 1;                       // hunter dps
        profiles[{ 3, ProfileKeyOfSpec(7) }] = 2;           // hunter bm
        profiles[{ 11, ProfileKeyOfSpec(27) }] = 3;         // druid feral_cat, no druid dps role profile
        profiles[{ 1, RoleTank }] = 4;                      // warrior tank

        auto value = [&](std::uint32_t classId, std::uint8_t role, std::uint8_t spec)
        {
            auto const it = FindProfile(profiles, classId, role, spec);
            return it == profiles.end() ? 0 : it->second;
        };
        CHECK(value(3, RoleDps, 7) == 2);   // the spec profile wins over the role profile
        CHECK(value(3, RoleDps, 8) == 1);   // mm has no profile: the role profile
        CHECK(value(3, RoleDps, 0) == 1);   // a module that sends no spec: as before protocol 18
        CHECK(value(3, 0, 7) == 2);         // spec alone
        CHECK(value(3, 0, 0) == 0);         // neither: the greedy allocator
        CHECK(value(3, RoleTank, 8) == 0);  // no tank or mm profile for the hunter
        CHECK(value(11, RoleDps, 27) == 3);
        CHECK(value(11, RoleDps, 25) == 0); // balance has no profile and no druid dps role profile: greedy
        CHECK(value(11, RoleDps, 0) == 0);
        CHECK(value(1, RoleTank, 3) == 4);  // warrior prot has no spec profile: the tank role profile
        CHECK(value(2, RoleTank, 5) == 0);  // another class's profiles are not used
    }

    if (g_failures == 0)
        std::printf("BotTalentSpecTest: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}

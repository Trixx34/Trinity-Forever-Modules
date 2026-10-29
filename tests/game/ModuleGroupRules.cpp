#include "ModuleGroupRules.h"
#include "tc_catch2.h"

#include <limits>

TEST_CASE("Raid subgroup action accepts only finite integral indexes in range", "[ModuleGroupRules]")
{
    uint8 subgroup = 99;
    CHECK(ModuleGroupRules::TryDecodeSubgroup(0.0f, 8, subgroup));
    CHECK(subgroup == 0);
    CHECK(ModuleGroupRules::TryDecodeSubgroup(7.0f, 8, subgroup));
    CHECK(subgroup == 7);

    CHECK_FALSE(ModuleGroupRules::TryDecodeSubgroup(-1.0f, 8, subgroup));
    CHECK(subgroup == 7); // invalid wire values do not overwrite the caller's value
    CHECK_FALSE(ModuleGroupRules::TryDecodeSubgroup(8.0f, 8, subgroup));
    CHECK_FALSE(ModuleGroupRules::TryDecodeSubgroup(2.5f, 8, subgroup));
    CHECK_FALSE(ModuleGroupRules::TryDecodeSubgroup(std::numeric_limits<float>::quiet_NaN(), 8, subgroup));
    CHECK_FALSE(ModuleGroupRules::TryDecodeSubgroup(std::numeric_limits<float>::infinity(), 8, subgroup));
    CHECK_FALSE(ModuleGroupRules::TryDecodeSubgroup(0.0f, 0, subgroup));
}

TEST_CASE("Raid subgroup retries succeed if already assigned even when the subgroup is full", "[ModuleGroupRules]")
{
    using ModuleGroupRules::SubgroupMove;
    CHECK(ModuleGroupRules::ClassifySubgroupMove(3, 3, false) == SubgroupMove::AlreadyAssigned);
    CHECK(ModuleGroupRules::ClassifySubgroupMove(2, 3, true) == SubgroupMove::Move);
    CHECK(ModuleGroupRules::ClassifySubgroupMove(2, 3, false) == SubgroupMove::Full);
}

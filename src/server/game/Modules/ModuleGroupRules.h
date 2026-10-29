/*
 * Pure validation for values carried by generic module group actions. Keep malformed wire values out of the
 * Group API, whose subgroup helpers expect a valid subgroup index.
 */

#ifndef TRINITYCORE_MODULE_GROUP_RULES_H
#define TRINITYCORE_MODULE_GROUP_RULES_H

#include "Define.h"

#include <cmath>

namespace ModuleGroupRules
{
enum class SubgroupMove : uint8
{
    AlreadyAssigned,
    Move,
    Full
};

inline bool TryDecodeSubgroup(float encoded, uint8 subgroupCount, uint8& subgroup)
{
    if (!std::isfinite(encoded) || encoded < 0.0f || encoded >= float(subgroupCount) || std::floor(encoded) != encoded)
        return false;

    subgroup = static_cast<uint8>(encoded);
    return true;
}

inline SubgroupMove ClassifySubgroupMove(uint8 current, uint8 requested, bool requestedHasSpace)
{
    if (current == requested)
        return SubgroupMove::AlreadyAssigned;
    return requestedHasSpace ? SubgroupMove::Move : SubgroupMove::Full;
}
}

#endif

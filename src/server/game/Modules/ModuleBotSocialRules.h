/*
 * The pure rules of the bot social actions (ModuleBotSocial.cpp), free of the world so that a standalone test can run them.
 */
#pragma once

#include "Define.h"

#include <string_view>

namespace ModuleBotSocial
{
// A gift's subject is 1 to 64 bytes.
constexpr size_t MaxGiftSubjectBytes = 64;

inline bool GiftSubjectValid(std::string_view subject)
{
    return !subject.empty() && subject.size() <= MaxGiftSubjectBytes;
}

// Whether a gift of `copper` and an item worth `itemValue` copper stays within `maxCopper`.
inline bool GiftWithinCap(uint64 copper, uint64 itemValue, uint64 maxCopper)
{
    return copper <= maxCopper && itemValue <= maxCopper;
}
}

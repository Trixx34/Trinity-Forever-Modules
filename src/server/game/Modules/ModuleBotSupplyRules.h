/*
 * Pure gear, junk and supply rules shared by the bot code and its tests: no player, item, vendor or map objects, only ids and numbers.
 * ModuleBotActions.cpp (junk selling, autogear, consume) and ModuleBotSupplies.cpp (buying, choosing what to eat) call these, so the decisions can be
 * tested without a world. Every function mirrors the expression it replaced; the values are unchanged.
 */

#ifndef TRINITY_MODULE_BOT_SUPPLY_RULES_H
#define TRINITY_MODULE_BOT_SUPPLY_RULES_H

#include "Define.h"
#include "ItemTemplate.h"

#include <algorithm>
#include <string>

namespace ModuleBotSupplyRules
{
// ---- junk ----

constexpr uint32 kQualityPoor = 0;      // grey
constexpr uint32 kQualityCommon = 1;    // white: the lowest quality the bot equips

// The pieces of each trade good (cloth, meat, leather...) a bot keeps when it sells its white junk: cloth is for First Aid, meat for Cooking, and the rest
// can go to the auction house, so only what is above this is sold to the vendor. Whole stacks are kept until the total reaches it.
constexpr uint32 kJunkKeepTradeGoods = 60;

// A white item a bot has no use for, so that a full bag can be emptied: trade goods, a spare bag, miscellaneous items. Never a quest item, one that
// starts a quest, a bag that is equipped (`equippedBagSlot`: the bags in the bag slots), or anything of another item class (consumables, projectiles,
// reagents, weapons, armour): those have their own rules.
inline bool IsWhiteJunk(uint32 quality, uint32 bonding, uint32 startQuest, bool equippedBagSlot, uint32 itemClass)
{
    if (quality != kQualityCommon || bonding == uint32(BIND_QUEST) || startQuest != 0)
        return false;
    if (equippedBagSlot)
        return false;
    switch (itemClass)
    {
        case ITEM_CLASS_TRADE_GOODS:
        case ITEM_CLASS_CONTAINER:
        case ITEM_CLASS_MISCELLANEOUS:
            return true;
        default:
            return false;
    }
}

// One stack of a trade good met while selling junk. `left` is how many pieces of this entry are still to be kept (start it at kJunkKeepTradeGoods).
// Returns true when the stack is kept (and takes it off `left`, never below 0), false when it is sold.
inline bool KeepTradeGoodStack(uint32& left, uint32 count)
{
    if (left == 0)
        return false;
    left = left > count ? left - count : 0;
    return true;
}

// ---- autogear ----

// AutoGearAction's spell_id flags: bit 0 a tank, bit 1 only wear what is better in the bags, bits 2-4 the gear profile (a spec-aware stat set).
constexpr uint32 kAutoGearTankBit = 1;
constexpr uint32 kAutoGearBagOnlyBit = 2;
constexpr uint32 kAutoGearProfileShift = 2;
constexpr uint32 kAutoGearProfileMask = 7;

inline bool AutoGearIsTank(uint32 flags) { return (flags & kAutoGearTankBit) != 0; }
inline bool AutoGearIsBagOnly(uint32 flags) { return (flags & kAutoGearBagOnlyBit) != 0; }
inline uint32 AutoGearProfileOf(uint32 flags) { return (flags >> kAutoGearProfileShift) & kAutoGearProfileMask; }

// Items that look unfinished are not handed out. `lowerName` is the item name in lower case.
inline bool IsPlaceholderItemName(std::string const& lowerName)
{
    return lowerName.empty() || lowerName.find("deprecated") != std::string::npos || lowerName.find("test") == 0 ||
        lowerName.find("unused") != std::string::npos || lowerName.find("[ph]") != std::string::npos || lowerName.rfind("zz", 0) == 0;
}

// What one item stat adds to the gear score: its amount when the class wants it, three times the amount against it for a primary stat the class does not
// use, nothing for any other stat.
inline int32 StatScore(bool wanted, bool primary, int32 amount)
{
    return wanted ? amount : (primary ? -3 * amount : 0);
}

// Higher is better: the item level first, then how well the stats suit the class.
inline int32 CombineGearScore(int32 itemLevel, int32 stats)
{
    return itemLevel * 10 + stats;
}

// Whether a candidate of `score` replaces the best pick so far (the first candidate always does; a tie keeps the earlier one).
inline bool ShouldReplacePick(bool havePick, int32 pickScore, int32 score)
{
    return !havePick || score > pickScore;
}

// A worn item is only replaced by a strictly higher item level (an equal one is kept, so two gear passes cannot undo each other).
inline bool IsItemLevelUpgrade(uint32 wornItemLevel, uint32 candidateItemLevel)
{
    return candidateItemLevel > wornItemLevel;
}

// A purchase is worth making only for a gain above zero.
inline int32 PositiveGain(int32 gain)
{
    return gain > 0 ? gain : 0;
}

// ---- supplies ----

enum class Kind : uint8
{
    None = 0,
    Food = 1,
    Drink = 2,
    HealthPotion = 3,   // layout 16
    ManaPotion = 4,
    Bandage = 5
};

// kConsume's spell_id: 1 food, 2 drink, 3 health potion, 4 mana potion, 5 bandage; anything else is Kind::None (refused).
inline Kind ConsumeKindFor(uint32 spellId)
{
    switch (spellId)
    {
        case 1: return Kind::Food;
        case 2: return Kind::Drink;
        case 3: return Kind::HealthPotion;
        case 4: return Kind::ManaPotion;
        case 5: return Kind::Bandage;
        default: return Kind::None;
    }
}

// A potion is drunk at once, in a fight or on the move; food, drink and bandages need the bot out of combat and standing still.
constexpr bool IsPotionKind(Kind kind)
{
    return kind == Kind::HealthPotion || kind == Kind::ManaPotion;
}

// The money a bot keeps back when it shops for anything but ammunition: `perLevel` copper for each level it has.
constexpr uint64 ReserveFor(uint32 level, uint32 perLevel)
{
    return uint64(level) * perLevel;
}

// What of `money` may be spent when `reserve` is kept back.
constexpr uint64 Spendable(uint64 money, uint64 reserve)
{
    return money > reserve ? money - reserve : 0;
}

// Whether a bot with mana (`manaUser`) can use an item of this kind at all (a drink or a mana potion needs mana).
constexpr bool KindFitsClass(Kind kind, bool manaUser)
{
    return kind != Kind::Drink && kind != Kind::ManaPotion ? true : manaUser;
}

static_assert(ReserveFor(60, 5) == 300 && ReserveFor(1, 0) == 0, "the reserve is perLevel copper for each level");
static_assert(Spendable(1000, 300) == 700 && Spendable(300, 300) == 0 && Spendable(100, 300) == 0, "money under the reserve is not spendable");

// How many more pieces to buy to bring a bot that carries `carried` up to `target`.
inline uint32 NeedToBuy(uint32 target, uint32 carried)
{
    return target > carried ? target - carried : 0;
}

// The price is per `buyCount` pieces; rounding up (and ignoring discounts) keeps the estimate on the safe side.
inline uint64 PerPieceCopper(uint32 buyPrice, uint32 buyCount)
{
    uint32 const per = buyCount != 0 ? buyCount : 1;
    return (uint64(buyPrice) + per - 1) / per;
}

// `count` pieces, limited to what `spendable` copper pays for at `perPiece` each (a free item is not limited).
inline uint32 AffordableCount(uint32 count, uint64 spendable, uint64 perPiece)
{
    if (perPiece > 0)
        return uint32(std::min<uint64>(count, spendable / perPiece));
    return count;
}

// A refused purchase (most often the money: a discount or a rounding the estimate missed) is retried this many times with a smaller number.
constexpr uint32 kBuyAttempts = 3;
inline uint32 ShrinkBuyCount(uint32 count)
{
    return count * 3 / 4;
}

// The most pieces (up to `count`) for which `fits(n)` holds, when it holds for every smaller number too: how much of a purchase the bags have room for.
// `fits` is asked for `count` first, then bisected (only for numbers between 1 and `count`).
template <typename Fits>
uint32 LargestFitting(uint32 count, Fits&& fits)
{
    if (fits(count))
        return count;
    uint32 low = 0, high = count;
    while (low < high)
    {
        uint32 const mid = low + (high - low + 1) / 2;
        if (fits(mid))
            low = mid;
        else
            high = mid - 1;
    }
    return low;
}

// A vendor item beats the best one so far: a higher required level, then a dearer item (it restores or hits more), then the lower entry so the choice is stable.
inline bool IsBetterVendorPick(int32 level, uint32 price, uint32 entry, int32 bestLevel, uint32 bestPrice, uint32 bestEntry)
{
    if (level != bestLevel)
        return level > bestLevel;
    if (price != bestPrice)
        return price > bestPrice;
    return entry < bestEntry;
}

// A carried item beats the best one so far to be eaten or drunk: the highest required level (it restores most); a smaller stack goes first so the stacks
// do not linger.
inline bool IsBetterCarried(int32 level, uint32 count, int32 bestLevel, uint32 bestCount)
{
    return level > bestLevel || (level == bestLevel && count < bestCount);
}
}

#endif

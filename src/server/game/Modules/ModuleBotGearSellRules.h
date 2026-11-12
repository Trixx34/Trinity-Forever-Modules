/*
 * Pure rule behind the kVendor "sell outclassed bag gear" bit (ModuleBotActions.cpp SellOutclassedGear): when a piece of gear in the bags is worth less than what
 * the bot wears, so a vendor may take it. The same rule as JudgeBagGear in MOD-Forever-Bots src/gear_score.*. No Player or Item types: the caller turns the
 * items into GearItem (with the score it computed), so the decisions are tested without a world. Items are judged by their numbers, never by item id.
 *
 * Also the per-role stat weights: what a role adds on top of the class stat list of GearScore (attack power, spell power, haste...), see RoleStatExtra.
 */

#ifndef TRINITYCORE_MODULE_BOT_GEAR_SELL_RULES_H
#define TRINITYCORE_MODULE_BOT_GEAR_SELL_RULES_H

#include "Define.h"
#include "ItemTemplate.h"

#include <cstddef>

namespace ModuleBotGearSell
{
// kVendor spell_id: bit 2048 sells outclassed bag gear (1024 is the white junk bit); bits 12-14 carry the gear profile (the GearProfile of AutoGearAction, 0-6)
// whose stat list judges the gear. Bit 15 and up stay free.
constexpr uint32 kServiceSellGearBit = 2048;
constexpr uint32 kGearProfileShift = 12;
constexpr uint32 kGearProfileMask = 7;

inline uint32 GearProfileOf(uint32 services) { return (services >> kGearProfileShift) & kGearProfileMask; }

// A stat the class list does not name, but the role wants: one point per point of attack power for a physical profile (0 and 1 strength/agility, 5 and 6 are
// tanks and take none), spell power for a caster or healer profile (2, 3, 4), and hit for both damage kinds. Added to the class score only by the sell rule.
inline int32 RoleStatExtra(uint32 profile, int32 stat, int32 amount)
{
    bool const physical = profile == 0 || profile == 1;
    bool const caster = profile == 2 || profile == 3 || profile == 4;
    switch (stat)
    {
        case ITEM_MOD_ATTACK_POWER: return physical ? amount : 0;
        case ITEM_MOD_SPELL_POWER: return caster ? amount : 0;
        case ITEM_MOD_HIT_RATING: return (physical || caster) ? amount : 0;
        default: return 0;
    }
}

constexpr int32 kSellMarginScore = 30;     // the worn item must score this much higher than the bag item
constexpr uint32 kMaxSellQuality = 2;      // green and lower only

enum class Verdict : uint8
{
    Sell,
    KeepNotEquipment, KeepQuestItem, KeepSetItem, KeepQuality, KeepNotUsable,
    KeepLevelLater,        // it needs a higher level than the bot has: it may become an upgrade
    KeepNothingWorn,       // a slot of that type is empty (or the worn list does not cover every slot of the type)
    KeepOtherSlotType,     // what is worn there is another inventory type (a 2-hand weapon against a 1-hand): not comparable
    KeepHeavierArmor,      // heavier body armour than what is worn: the equip pass may still swap to it
    KeepCouldBeBetter      // not outclassed by every worn item by the margin
};

inline char const* VerdictName(Verdict verdict)
{
    switch (verdict)
    {
        case Verdict::Sell: return "outclassed";
        case Verdict::KeepNotEquipment: return "keep_not_equipment";
        case Verdict::KeepQuestItem: return "keep_quest_item";
        case Verdict::KeepSetItem: return "keep_set_item";
        case Verdict::KeepQuality: return "keep_quality";
        case Verdict::KeepNotUsable: return "keep_not_usable";
        case Verdict::KeepLevelLater: return "keep_level_later";
        case Verdict::KeepNothingWorn: return "keep_nothing_worn";
        case Verdict::KeepOtherSlotType: return "keep_other_slot_type";
        case Verdict::KeepHeavierArmor: return "keep_heavier_armor";
        case Verdict::KeepCouldBeBetter: return "keep_could_be_better";
    }
    return "keep";
}

struct GearItem
{
    uint32 ItemLevel = 0;
    uint32 Quality = 0;            // 0 grey, 1 white, 2 green, 3 blue, 4 epic
    uint32 RequiredLevel = 0;
    uint32 InventoryType = 0;      // 0 = not equipment
    uint32 ArmorSubclass = 0;      // 1 cloth, 2 leather, 3 mail, 4 plate; 0 = not body armour
    bool QuestItem = false;
    bool SetItem = false;
    bool Usable = true;
    int32 Score = 0;               // the gear score the caller computed
};

// Bag gear is outclassed only when every slot it could fill holds a worn item of the same inventory type with an item level at least as high and a score higher
// by kSellMarginScore. `worn` lists the items in those slots; `slots` is how many slots the type has (2 for rings, trinkets and one-hand weapons).
inline Verdict JudgeBagGear(uint32 botLevel, GearItem const& bag, GearItem const* worn, std::size_t wornCount, std::size_t slots)
{
    if (bag.InventoryType == 0)
        return Verdict::KeepNotEquipment;
    if (bag.QuestItem)
        return Verdict::KeepQuestItem;
    if (bag.SetItem)
        return Verdict::KeepSetItem;
    if (bag.Quality > kMaxSellQuality)
        return Verdict::KeepQuality;
    if (!bag.Usable)
        return Verdict::KeepNotUsable;
    if (bag.RequiredLevel > botLevel)
        return Verdict::KeepLevelLater;
    if (slots == 0 || wornCount < slots)
        return Verdict::KeepNothingWorn;
    for (std::size_t i = 0; i < wornCount; ++i)
    {
        GearItem const& w = worn[i];
        if (w.InventoryType != bag.InventoryType)
            return Verdict::KeepOtherSlotType;
        if (bag.ArmorSubclass > w.ArmorSubclass)
            return Verdict::KeepHeavierArmor;
        if (w.ItemLevel < bag.ItemLevel || w.Score < bag.Score + kSellMarginScore)
            return Verdict::KeepCouldBeBetter;
    }
    return Verdict::Sell;
}
}

#endif

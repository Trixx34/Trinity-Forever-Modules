/*
 * Pure gear rules shared by the bot code and its tests: no player, item or map objects, only ids and numbers.
 */

#ifndef TRINITY_BOT_GEAR_RULES_H
#define TRINITY_BOT_GEAR_RULES_H

#include "Define.h"
#include "ItemTemplate.h"
#include "SharedDefines.h"

#include <algorithm>
#include <vector>

namespace BotGearRules
{
// The heaviest armour a class wears at a level (ITEM_SUBCLASS_ARMOR_*): cloth, leather, mail or plate. Lighter armour is allowed too.
inline uint32 MaxArmorSubclass(uint8 classId, uint32 level)
{
    switch (classId)
    {
        case CLASS_WARRIOR:
        case CLASS_PALADIN: return level >= 40 ? ITEM_SUBCLASS_ARMOR_PLATE : ITEM_SUBCLASS_ARMOR_MAIL;
        case CLASS_HUNTER:
        case CLASS_SHAMAN: return level >= 40 ? ITEM_SUBCLASS_ARMOR_MAIL : ITEM_SUBCLASS_ARMOR_LEATHER;
        case CLASS_ROGUE:
        case CLASS_DRUID: return ITEM_SUBCLASS_ARMOR_LEATHER;
        default: return ITEM_SUBCLASS_ARMOR_CLOTH; // priest, mage, warlock
    }
}

// True for the inventory types ArmorTooHeavy judges: the armour worn on the body.
inline bool IsBodyArmorSlot(uint32 inventoryType)
{
    switch (inventoryType)
    {
        case INVTYPE_HEAD: case INVTYPE_SHOULDERS: case INVTYPE_CHEST: case INVTYPE_ROBE: case INVTYPE_WAIST: case INVTYPE_LEGS:
        case INVTYPE_FEET: case INVTYPE_WRISTS: case INVTYPE_HANDS: return true;
        default: return false;
    }
}

// True for body-slot armour that is lighter than the class maximum (cloth for a shaman, leather for a hunter at 40+, ...). From level 40 a
// class wears its maximum armour; the leveling path (below 40) keeps taking whatever scores best, so nothing is lighter there.
inline bool ArmorLighterThanMax(uint8 classId, uint32 level, uint32 itemClass, uint32 subclass, uint32 inventoryType)
{
    if (level < 40 || itemClass != ITEM_CLASS_ARMOR || !IsBodyArmorSlot(inventoryType))
        return false;
    return subclass > ITEM_SUBCLASS_ARMOR_MISCELLANEOUS && subclass < MaxArmorSubclass(classId, level);
}

// Whether a worn item stays: the item level rule (only a strictly higher item level replaces it), except that worn armour lighter than
// the class maximum is replaced by an item that is not lighter whatever the item levels. `newLighter` false means the candidate is of the
// maximum subclass (or not judged), so a candidate of the right subclass exists for the slot.
inline bool KeepWorn(uint32 wornItemLevel, uint32 newItemLevel, bool wornLighter, bool newLighter)
{
    if (wornLighter && !newLighter)
        return false;
    if (!wornLighter && newLighter)
        return true; // never swap max-subclass armour for lighter armour, however high its item level
    return newItemLevel <= wornItemLevel;
}

// One item that could go in a slot: `Lighter` is ArmorLighterThanMax for it.
struct SlotCandidate
{
    uint32 Id = 0;
    int32 Score = 0;
    bool Lighter = false;
};

// The best `count` distinct items for a slot (count 2 for rings and trinkets: the first and second slot), best first. When any candidate is
// not lighter than the class maximum, the lighter ones are dropped; when none is, the lighter ones stay as the fallback. A tie keeps the
// earlier candidate. An id listed twice is chosen once (a unique-equipped item cannot be worn twice).
inline std::vector<uint32> ChooseForSlot(std::vector<SlotCandidate> candidates, size_t count)
{
    if (std::any_of(candidates.begin(), candidates.end(), [](SlotCandidate const& c) { return !c.Lighter; }))
        candidates.erase(std::remove_if(candidates.begin(), candidates.end(), [](SlotCandidate const& c) { return c.Lighter; }), candidates.end());
    std::stable_sort(candidates.begin(), candidates.end(), [](SlotCandidate const& a, SlotCandidate const& b) { return a.Score > b.Score; });
    std::vector<uint32> chosen;
    for (SlotCandidate const& c : candidates)
    {
        if (chosen.size() >= count)
            break;
        if (std::find(chosen.begin(), chosen.end(), c.Id) == chosen.end())
            chosen.push_back(c.Id);
    }
    return chosen;
}

// True for armour in the body slots that is heavier than the class wears. Jewellery, cloaks and shields are not judged here.
inline bool ArmorTooHeavy(uint8 classId, uint32 level, uint32 itemClass, uint32 subclass, uint32 inventoryType)
{
    if (itemClass != ITEM_CLASS_ARMOR)
        return false;
    switch (inventoryType)
    {
        case INVTYPE_HEAD: case INVTYPE_SHOULDERS: case INVTYPE_CHEST: case INVTYPE_ROBE: case INVTYPE_WAIST: case INVTYPE_LEGS:
        case INVTYPE_FEET: case INVTYPE_WRISTS: case INVTYPE_HANDS:
            return subclass > ITEM_SUBCLASS_ARMOR_MISCELLANEOUS && subclass <= ITEM_SUBCLASS_ARMOR_PLATE && subclass > MaxArmorSubclass(classId, level);
        default: return false;
    }
}
}

#endif

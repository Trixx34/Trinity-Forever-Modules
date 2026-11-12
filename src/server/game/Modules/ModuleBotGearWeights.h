/*
 * Role stat weights for bot gear (Bot.Gear.RoleWeights): one table, RoleWeight(classId, profile, itemMod), that the equip passes, the purchase gain and the
 * sell rule all score with, so they agree on what a better item is. Pure: no Player or Item types, only ids and numbers, tested in tests/game/ModuleBotGearWeights.cpp.
 *
 * The numbers are those of MOD-Forever-Bots src/gear_score.cpp (WeightsOf per Archetype, kPrimary 3, item level x 10); tests/game/ModuleBotGearWeights.cpp
 * and the module's tests/gear_score_test.cpp pin the same values, so a change on one side shows up as a failing test on the other. Parity notes:
 *  - The module picks the archetype from a role (0 class default, 1 tank, 2 healer, 3 damage); the core only receives the gear profile (0-6, see
 *    ModuleBotActions.cpp GearProfile), so ArchetypeOf derives it from (class, profile): 5 and 6 are tanks, 4 and 2 are healers for the classes that heal
 *    (a mage or warlock with 2 is a caster), 3 is a caster, 0 and 1 are the class's damage archetype (hunter, rogue and druid by class).
 *  - A stat with a zero or negative amount is skipped, as in the class-list score (StatScore callers), the module does not skip them.
 *  - Items are judged by their numbers only, never by item id.
 */

#ifndef TRINITYCORE_MODULE_BOT_GEAR_WEIGHTS_H
#define TRINITYCORE_MODULE_BOT_GEAR_WEIGHTS_H

#include "Define.h"
#include "ItemTemplate.h"
#include "SharedDefines.h"

namespace ModuleBotGearWeights
{
enum class Archetype : uint8 { Tank, MeleeStrength, MeleeAgility, Hunter, Caster, Healer };

constexpr int32 kPrimary = 3;          // the weight of a primary stat the role wants (and against one it does not use)
constexpr int32 kItemLevelWeight = 10; // as ModuleBotSupplyRules::CombineGearScore

// profile: the GearProfile of AutoGearAction (0 StrStam, 1 AgiStam, 2 IntSpiritStam, 3 IntCrit, 4 IntSpiritHaste, 5 StrStamDef, 6 AgiStamDef).
inline Archetype ArchetypeOf(uint32 classId, uint32 profile)
{
    if (profile == 5 || profile == 6)
        return Archetype::Tank;
    if (classId == CLASS_HUNTER)
        return Archetype::Hunter;
    if (classId == CLASS_ROGUE)
        return Archetype::MeleeAgility;
    bool const healsClass = classId == CLASS_PRIEST || classId == CLASS_PALADIN || classId == CLASS_SHAMAN || classId == CLASS_DRUID;
    switch (profile)
    {
        case 4: return Archetype::Healer;
        case 3: return Archetype::Caster;
        case 2: return healsClass ? Archetype::Healer : Archetype::Caster;
        case 1: return Archetype::MeleeAgility;
        default: break;
    }
    // profile 0 is also what a module build that sends no profile gives: the class's own default
    switch (classId)
    {
        case CLASS_WARRIOR:
        case CLASS_PALADIN: return Archetype::MeleeStrength;
        case CLASS_PRIEST:
        case CLASS_SHAMAN:
        case CLASS_DRUID: return Archetype::Healer;
        case CLASS_MAGE:
        case CLASS_WARLOCK: return Archetype::Caster;
        default: return Archetype::MeleeAgility;
    }
}

// The weight per point of one item stat (ITEM_MOD_*) for an archetype; 0 for a stat the table does not name. Values as WeightsOf in gear_score.cpp.
inline int32 ArchetypeWeight(Archetype archetype, int32 itemMod)
{
    switch (archetype)
    {
        case Archetype::Tank:
            switch (itemMod)
            {
                case ITEM_MOD_STAMINA: return 2;
                case ITEM_MOD_STRENGTH: return kPrimary;
                case ITEM_MOD_AGILITY: return 1;
                case ITEM_MOD_DEFENSE_SKILL_RATING: return 2;
                case ITEM_MOD_DODGE_RATING: return 2;
                case ITEM_MOD_PARRY_RATING: return 2;
                case ITEM_MOD_BLOCK_RATING: return 1;
                case ITEM_MOD_INTELLECT: return -kPrimary;
                case ITEM_MOD_SPIRIT: return -kPrimary;
                case ITEM_MOD_ATTACK_POWER: return 1;
                default: return 0;
            }
        case Archetype::MeleeStrength:
            switch (itemMod)
            {
                case ITEM_MOD_STRENGTH: return kPrimary;
                case ITEM_MOD_AGILITY: return 1;
                case ITEM_MOD_STAMINA: return 1;
                case ITEM_MOD_ATTACK_POWER: return 1;
                case ITEM_MOD_CRIT_RATING: return 1;
                case ITEM_MOD_HIT_RATING: return 1;
                case ITEM_MOD_HASTE_RATING: return 1;
                case ITEM_MOD_INTELLECT: return -kPrimary;
                case ITEM_MOD_SPIRIT: return -kPrimary;
                default: return 0;
            }
        case Archetype::MeleeAgility:
            switch (itemMod)
            {
                case ITEM_MOD_AGILITY: return kPrimary;
                case ITEM_MOD_STRENGTH: return 1;
                case ITEM_MOD_STAMINA: return 1;
                case ITEM_MOD_ATTACK_POWER: return 1;
                case ITEM_MOD_CRIT_RATING: return 1;
                case ITEM_MOD_HIT_RATING: return 1;
                case ITEM_MOD_HASTE_RATING: return 1;
                case ITEM_MOD_INTELLECT: return -kPrimary;
                case ITEM_MOD_SPIRIT: return -kPrimary;
                default: return 0;
            }
        case Archetype::Hunter:
            switch (itemMod)
            {
                case ITEM_MOD_AGILITY: return kPrimary;
                case ITEM_MOD_INTELLECT: return 1;
                case ITEM_MOD_STAMINA: return 1;
                case ITEM_MOD_ATTACK_POWER: return 1;
                case ITEM_MOD_CRIT_RATING: return 1;
                case ITEM_MOD_HIT_RATING: return 1;
                case ITEM_MOD_HASTE_RATING: return 1;
                case ITEM_MOD_STRENGTH: return -kPrimary;
                case ITEM_MOD_SPIRIT: return -kPrimary;
                default: return 0;
            }
        case Archetype::Caster:
            switch (itemMod)
            {
                case ITEM_MOD_INTELLECT: return kPrimary;
                case ITEM_MOD_SPELL_POWER: return 1;
                case ITEM_MOD_STAMINA: return 1;
                case ITEM_MOD_SPIRIT: return 1;
                case ITEM_MOD_CRIT_RATING: return 1;
                case ITEM_MOD_HIT_RATING: return 1;
                case ITEM_MOD_HASTE_RATING: return 1;
                case ITEM_MOD_STRENGTH: return -kPrimary;
                case ITEM_MOD_AGILITY: return -kPrimary;
                default: return 0;
            }
        case Archetype::Healer:
            switch (itemMod)
            {
                case ITEM_MOD_INTELLECT: return kPrimary;
                case ITEM_MOD_SPIRIT: return kPrimary;
                case ITEM_MOD_SPELL_POWER: return 1;
                case ITEM_MOD_STAMINA: return 1;
                case ITEM_MOD_CRIT_RATING: return 1;
                case ITEM_MOD_HASTE_RATING: return 1;
                case ITEM_MOD_STRENGTH: return -kPrimary;
                case ITEM_MOD_AGILITY: return -kPrimary;
                default: return 0;
            }
    }
    return 0;
}

// The one table: what a point of item stat `itemMod` is worth to a class playing a gear profile.
inline int32 RoleWeight(uint32 classId, uint32 profile, int32 itemMod)
{
    return ArchetypeWeight(ArchetypeOf(classId, profile), itemMod);
}
}

#endif

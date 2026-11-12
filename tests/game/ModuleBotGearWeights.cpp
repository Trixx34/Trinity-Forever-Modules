#include "tc_catch2.h"
#include "ModuleBotGearWeights.h"

using namespace ModuleBotGearWeights;

namespace
{
// A profile as AutoGearAction numbers them.
constexpr uint32 kStrStam = 0, kAgiStam = 1, kIntSpiritStam = 2, kIntCrit = 3, kIntSpiritHaste = 4, kStrStamDef = 5, kAgiStamDef = 6;
}

TEST_CASE("The archetype comes from the class and the gear profile", "[ModuleBotGearWeights]")
{
    CHECK(ArchetypeOf(CLASS_WARRIOR, kStrStamDef) == Archetype::Tank);
    CHECK(ArchetypeOf(CLASS_PALADIN, kStrStamDef) == Archetype::Tank);
    CHECK(ArchetypeOf(CLASS_DRUID, kAgiStamDef) == Archetype::Tank);
    CHECK(ArchetypeOf(CLASS_WARRIOR, kStrStam) == Archetype::MeleeStrength);
    CHECK(ArchetypeOf(CLASS_PALADIN, kStrStam) == Archetype::MeleeStrength);
    // hunter and rogue share profile 1: the class splits them
    CHECK(ArchetypeOf(CLASS_HUNTER, kAgiStam) == Archetype::Hunter);
    CHECK(ArchetypeOf(CLASS_HUNTER, kStrStam) == Archetype::Hunter);
    CHECK(ArchetypeOf(CLASS_ROGUE, kAgiStam) == Archetype::MeleeAgility);
    CHECK(ArchetypeOf(CLASS_ROGUE, kStrStam) == Archetype::MeleeAgility);
    CHECK(ArchetypeOf(CLASS_SHAMAN, kAgiStam) == Archetype::MeleeAgility);   // enhancement
    CHECK(ArchetypeOf(CLASS_PRIEST, kIntSpiritHaste) == Archetype::Healer);
    CHECK(ArchetypeOf(CLASS_DRUID, kIntSpiritHaste) == Archetype::Healer);
    CHECK(ArchetypeOf(CLASS_PRIEST, kIntCrit) == Archetype::Caster);
    CHECK(ArchetypeOf(CLASS_MAGE, kIntCrit) == Archetype::Caster);
    CHECK(ArchetypeOf(CLASS_MAGE, kIntSpiritStam) == Archetype::Caster);
    CHECK(ArchetypeOf(CLASS_WARLOCK, kIntSpiritStam) == Archetype::Caster);
    CHECK(ArchetypeOf(CLASS_PRIEST, kIntSpiritStam) == Archetype::Healer);
    // no profile sent (0): the class default
    CHECK(ArchetypeOf(CLASS_PRIEST, kStrStam) == Archetype::Healer);
    CHECK(ArchetypeOf(CLASS_MAGE, kStrStam) == Archetype::Caster);
    CHECK(ArchetypeOf(CLASS_ROGUE, kStrStam) == Archetype::MeleeAgility);
}

// Parity with MOD-Forever-Bots src/gear_score.cpp WeightsOf (tests/gear_score_test.cpp there): the same numbers, per archetype. If this fails after a change
// to the module's table, change both sides together.
TEST_CASE("Role weights match the module's gear_score table", "[ModuleBotGearWeights]")
{
    auto const w = [](Archetype a, int32 stat) { return ArchetypeWeight(a, stat); };

    CHECK(w(Archetype::Tank, ITEM_MOD_STAMINA) == 2);
    CHECK(w(Archetype::Tank, ITEM_MOD_STRENGTH) == 3);
    CHECK(w(Archetype::Tank, ITEM_MOD_AGILITY) == 1);
    CHECK(w(Archetype::Tank, ITEM_MOD_DEFENSE_SKILL_RATING) == 2);
    CHECK(w(Archetype::Tank, ITEM_MOD_DODGE_RATING) == 2);
    CHECK(w(Archetype::Tank, ITEM_MOD_PARRY_RATING) == 2);
    CHECK(w(Archetype::Tank, ITEM_MOD_BLOCK_RATING) == 1);
    CHECK(w(Archetype::Tank, ITEM_MOD_INTELLECT) == -3);
    CHECK(w(Archetype::Tank, ITEM_MOD_SPIRIT) == -3);
    CHECK(w(Archetype::Tank, ITEM_MOD_SPELL_POWER) == 0);
    CHECK(w(Archetype::Tank, ITEM_MOD_ATTACK_POWER) == 1);
    CHECK(w(Archetype::Tank, ITEM_MOD_HIT_RATING) == 0);

    CHECK(w(Archetype::MeleeStrength, ITEM_MOD_STRENGTH) == 3);
    CHECK(w(Archetype::MeleeStrength, ITEM_MOD_AGILITY) == 1);
    CHECK(w(Archetype::MeleeStrength, ITEM_MOD_STAMINA) == 1);
    CHECK(w(Archetype::MeleeStrength, ITEM_MOD_ATTACK_POWER) == 1);
    CHECK(w(Archetype::MeleeStrength, ITEM_MOD_CRIT_RATING) == 1);
    CHECK(w(Archetype::MeleeStrength, ITEM_MOD_HIT_RATING) == 1);
    CHECK(w(Archetype::MeleeStrength, ITEM_MOD_HASTE_RATING) == 1);
    CHECK(w(Archetype::MeleeStrength, ITEM_MOD_INTELLECT) == -3);
    CHECK(w(Archetype::MeleeStrength, ITEM_MOD_SPIRIT) == -3);
    CHECK(w(Archetype::MeleeStrength, ITEM_MOD_DODGE_RATING) == 0);

    CHECK(w(Archetype::MeleeAgility, ITEM_MOD_AGILITY) == 3);
    CHECK(w(Archetype::MeleeAgility, ITEM_MOD_STRENGTH) == 1);
    CHECK(w(Archetype::MeleeAgility, ITEM_MOD_STAMINA) == 1);
    CHECK(w(Archetype::MeleeAgility, ITEM_MOD_ATTACK_POWER) == 1);
    CHECK(w(Archetype::MeleeAgility, ITEM_MOD_CRIT_RATING) == 1);
    CHECK(w(Archetype::MeleeAgility, ITEM_MOD_HIT_RATING) == 1);
    CHECK(w(Archetype::MeleeAgility, ITEM_MOD_HASTE_RATING) == 1);
    CHECK(w(Archetype::MeleeAgility, ITEM_MOD_INTELLECT) == -3);
    CHECK(w(Archetype::MeleeAgility, ITEM_MOD_SPIRIT) == -3);

    CHECK(w(Archetype::Hunter, ITEM_MOD_AGILITY) == 3);
    CHECK(w(Archetype::Hunter, ITEM_MOD_INTELLECT) == 1);
    CHECK(w(Archetype::Hunter, ITEM_MOD_STAMINA) == 1);
    CHECK(w(Archetype::Hunter, ITEM_MOD_ATTACK_POWER) == 1);
    CHECK(w(Archetype::Hunter, ITEM_MOD_CRIT_RATING) == 1);
    CHECK(w(Archetype::Hunter, ITEM_MOD_HIT_RATING) == 1);
    CHECK(w(Archetype::Hunter, ITEM_MOD_HASTE_RATING) == 1);
    CHECK(w(Archetype::Hunter, ITEM_MOD_STRENGTH) == -3);
    CHECK(w(Archetype::Hunter, ITEM_MOD_SPIRIT) == -3);

    CHECK(w(Archetype::Caster, ITEM_MOD_INTELLECT) == 3);
    CHECK(w(Archetype::Caster, ITEM_MOD_SPELL_POWER) == 1);
    CHECK(w(Archetype::Caster, ITEM_MOD_STAMINA) == 1);
    CHECK(w(Archetype::Caster, ITEM_MOD_SPIRIT) == 1);
    CHECK(w(Archetype::Caster, ITEM_MOD_CRIT_RATING) == 1);
    CHECK(w(Archetype::Caster, ITEM_MOD_HIT_RATING) == 1);
    CHECK(w(Archetype::Caster, ITEM_MOD_HASTE_RATING) == 1);
    CHECK(w(Archetype::Caster, ITEM_MOD_STRENGTH) == -3);
    CHECK(w(Archetype::Caster, ITEM_MOD_AGILITY) == -3);

    CHECK(w(Archetype::Healer, ITEM_MOD_INTELLECT) == 3);
    CHECK(w(Archetype::Healer, ITEM_MOD_SPIRIT) == 3);
    CHECK(w(Archetype::Healer, ITEM_MOD_SPELL_POWER) == 1);
    CHECK(w(Archetype::Healer, ITEM_MOD_STAMINA) == 1);
    CHECK(w(Archetype::Healer, ITEM_MOD_CRIT_RATING) == 1);
    CHECK(w(Archetype::Healer, ITEM_MOD_HASTE_RATING) == 1);
    CHECK(w(Archetype::Healer, ITEM_MOD_HIT_RATING) == 0);
    CHECK(w(Archetype::Healer, ITEM_MOD_STRENGTH) == -3);
    CHECK(w(Archetype::Healer, ITEM_MOD_AGILITY) == -3);

    CHECK(kItemLevelWeight == 10);
    CHECK(kPrimary == 3);
}

TEST_CASE("RoleWeight is the archetype's weight for the class and profile, and 0 for a stat the table does not name", "[ModuleBotGearWeights]")
{
    CHECK(RoleWeight(CLASS_WARRIOR, kStrStamDef, ITEM_MOD_PARRY_RATING) == 2);
    CHECK(RoleWeight(CLASS_WARRIOR, kStrStam, ITEM_MOD_PARRY_RATING) == 0);
    CHECK(RoleWeight(CLASS_HUNTER, kAgiStam, ITEM_MOD_STRENGTH) == -3);
    CHECK(RoleWeight(CLASS_ROGUE, kAgiStam, ITEM_MOD_STRENGTH) == 1);
    CHECK(RoleWeight(CLASS_PRIEST, kIntSpiritHaste, ITEM_MOD_SPIRIT) == 3);
    CHECK(RoleWeight(CLASS_MAGE, kIntCrit, ITEM_MOD_SPIRIT) == 1);
    CHECK(RoleWeight(CLASS_WARRIOR, kStrStam, 99) == 0);
    CHECK(RoleWeight(CLASS_WARRIOR, kStrStam, 0) == 0);
}

TEST_CASE("The role table agrees with itself between sell and equip: a stat the role wants never counts less to one pass", "[ModuleBotGearWeights]")
{
    // The old class list gave attack power, spell power and hit no weight for the equip passes; the table does for the roles that use them.
    CHECK(RoleWeight(CLASS_ROGUE, kAgiStam, ITEM_MOD_ATTACK_POWER) == 1);
    CHECK(RoleWeight(CLASS_MAGE, kIntCrit, ITEM_MOD_SPELL_POWER) == 1);
    CHECK(RoleWeight(CLASS_PRIEST, kIntSpiritHaste, ITEM_MOD_SPELL_POWER) == 1);
    // and a primary a role does not use counts against it
    CHECK(RoleWeight(CLASS_MAGE, kIntCrit, ITEM_MOD_AGILITY) < 0);
    CHECK(RoleWeight(CLASS_WARRIOR, kStrStam, ITEM_MOD_INTELLECT) < 0);
}

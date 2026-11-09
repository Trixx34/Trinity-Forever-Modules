#include "tc_catch2.h"
#include "BotGearRules.h"

TEST_CASE("Armour a class wears follows the level", "[BotGearRules]")
{
    CHECK(BotGearRules::MaxArmorSubclass(CLASS_WARRIOR, 39) == ITEM_SUBCLASS_ARMOR_MAIL);
    CHECK(BotGearRules::MaxArmorSubclass(CLASS_WARRIOR, 40) == ITEM_SUBCLASS_ARMOR_PLATE);
    CHECK(BotGearRules::MaxArmorSubclass(CLASS_PALADIN, 40) == ITEM_SUBCLASS_ARMOR_PLATE);
    CHECK(BotGearRules::MaxArmorSubclass(CLASS_HUNTER, 39) == ITEM_SUBCLASS_ARMOR_LEATHER);
    CHECK(BotGearRules::MaxArmorSubclass(CLASS_SHAMAN, 40) == ITEM_SUBCLASS_ARMOR_MAIL);
    CHECK(BotGearRules::MaxArmorSubclass(CLASS_ROGUE, 60) == ITEM_SUBCLASS_ARMOR_LEATHER);
    CHECK(BotGearRules::MaxArmorSubclass(CLASS_DRUID, 60) == ITEM_SUBCLASS_ARMOR_LEATHER);
    for (uint8 classId : { uint8(CLASS_PRIEST), uint8(CLASS_MAGE), uint8(CLASS_WARLOCK) })
        CHECK(BotGearRules::MaxArmorSubclass(classId, 60) == ITEM_SUBCLASS_ARMOR_CLOTH);
}

TEST_CASE("Armour heavier than the class wears is refused in the body slots", "[BotGearRules]")
{
    // a mage never wears leather, mail or plate chest pieces
    CHECK_FALSE(BotGearRules::ArmorTooHeavy(CLASS_MAGE, 60, ITEM_CLASS_ARMOR, ITEM_SUBCLASS_ARMOR_CLOTH, INVTYPE_CHEST));
    CHECK(BotGearRules::ArmorTooHeavy(CLASS_MAGE, 60, ITEM_CLASS_ARMOR, ITEM_SUBCLASS_ARMOR_LEATHER, INVTYPE_CHEST));
    CHECK(BotGearRules::ArmorTooHeavy(CLASS_MAGE, 60, ITEM_CLASS_ARMOR, ITEM_SUBCLASS_ARMOR_PLATE, INVTYPE_HEAD));
    // a warrior before 40 refuses plate, after 40 wears it; lighter armour is always fine
    CHECK(BotGearRules::ArmorTooHeavy(CLASS_WARRIOR, 39, ITEM_CLASS_ARMOR, ITEM_SUBCLASS_ARMOR_PLATE, INVTYPE_LEGS));
    CHECK_FALSE(BotGearRules::ArmorTooHeavy(CLASS_WARRIOR, 40, ITEM_CLASS_ARMOR, ITEM_SUBCLASS_ARMOR_PLATE, INVTYPE_LEGS));
    CHECK_FALSE(BotGearRules::ArmorTooHeavy(CLASS_WARRIOR, 10, ITEM_CLASS_ARMOR, ITEM_SUBCLASS_ARMOR_CLOTH, INVTYPE_LEGS));
}

TEST_CASE("Only body-slot armour is judged by weight", "[BotGearRules]")
{
    // jewellery, cloaks and shields carry armour subclasses of their own and are not judged
    CHECK_FALSE(BotGearRules::ArmorTooHeavy(CLASS_MAGE, 60, ITEM_CLASS_ARMOR, ITEM_SUBCLASS_ARMOR_PLATE, INVTYPE_CLOAK));
    CHECK_FALSE(BotGearRules::ArmorTooHeavy(CLASS_MAGE, 60, ITEM_CLASS_ARMOR, ITEM_SUBCLASS_ARMOR_PLATE, INVTYPE_SHIELD));
    CHECK_FALSE(BotGearRules::ArmorTooHeavy(CLASS_MAGE, 60, ITEM_CLASS_ARMOR, ITEM_SUBCLASS_ARMOR_PLATE, INVTYPE_FINGER));
    // the miscellaneous subclass and anything that is not armour are never too heavy
    CHECK_FALSE(BotGearRules::ArmorTooHeavy(CLASS_MAGE, 60, ITEM_CLASS_ARMOR, ITEM_SUBCLASS_ARMOR_MISCELLANEOUS, INVTYPE_CHEST));
    CHECK_FALSE(BotGearRules::ArmorTooHeavy(CLASS_MAGE, 60, ITEM_CLASS_WEAPON, ITEM_SUBCLASS_ARMOR_PLATE, INVTYPE_CHEST));
}

TEST_CASE("Armour lighter than the class maximum is spotted", "[BotGearRules]")
{
    // a shaman at 40+ wears mail: cloth and leather are lighter, mail is not
    CHECK(BotGearRules::ArmorLighterThanMax(CLASS_SHAMAN, 40, ITEM_CLASS_ARMOR, ITEM_SUBCLASS_ARMOR_CLOTH, INVTYPE_CHEST));
    CHECK(BotGearRules::ArmorLighterThanMax(CLASS_SHAMAN, 60, ITEM_CLASS_ARMOR, ITEM_SUBCLASS_ARMOR_LEATHER, INVTYPE_LEGS));
    CHECK_FALSE(BotGearRules::ArmorLighterThanMax(CLASS_SHAMAN, 60, ITEM_CLASS_ARMOR, ITEM_SUBCLASS_ARMOR_MAIL, INVTYPE_CHEST));
    // a hunter at 40+ takes mail over leather
    CHECK(BotGearRules::ArmorLighterThanMax(CLASS_HUNTER, 40, ITEM_CLASS_ARMOR, ITEM_SUBCLASS_ARMOR_LEATHER, INVTYPE_HEAD));
    CHECK_FALSE(BotGearRules::ArmorLighterThanMax(CLASS_HUNTER, 40, ITEM_CLASS_ARMOR, ITEM_SUBCLASS_ARMOR_MAIL, INVTYPE_HEAD));
    // a druid takes leather over cloth
    CHECK(BotGearRules::ArmorLighterThanMax(CLASS_DRUID, 60, ITEM_CLASS_ARMOR, ITEM_SUBCLASS_ARMOR_CLOTH, INVTYPE_ROBE));
    CHECK_FALSE(BotGearRules::ArmorLighterThanMax(CLASS_DRUID, 60, ITEM_CLASS_ARMOR, ITEM_SUBCLASS_ARMOR_LEATHER, INVTYPE_ROBE));
    // a cloth class has nothing lighter
    CHECK_FALSE(BotGearRules::ArmorLighterThanMax(CLASS_MAGE, 60, ITEM_CLASS_ARMOR, ITEM_SUBCLASS_ARMOR_CLOTH, INVTYPE_CHEST));
    // not judged: jewellery, cloaks, weapons and the miscellaneous subclass
    CHECK_FALSE(BotGearRules::ArmorLighterThanMax(CLASS_SHAMAN, 60, ITEM_CLASS_ARMOR, ITEM_SUBCLASS_ARMOR_CLOTH, INVTYPE_CLOAK));
    CHECK_FALSE(BotGearRules::ArmorLighterThanMax(CLASS_SHAMAN, 60, ITEM_CLASS_ARMOR, ITEM_SUBCLASS_ARMOR_MISCELLANEOUS, INVTYPE_FINGER));
    CHECK_FALSE(BotGearRules::ArmorLighterThanMax(CLASS_SHAMAN, 60, ITEM_CLASS_WEAPON, ITEM_SUBCLASS_ARMOR_CLOTH, INVTYPE_CHEST));
}

TEST_CASE("The armour preference follows the level-based class maximum", "[BotGearRules]")
{
    auto const lighter = [](uint8 classId, uint32 level, uint32 subclass)
    {
        return BotGearRules::ArmorLighterThanMax(classId, level, ITEM_CLASS_ARMOR, subclass, INVTYPE_CHEST);
    };
    // a druid prefers leather over cloth at every level
    for (uint32 level : { 1u, 20u, 39u, 40u, 60u })
    {
        CHECK(BotGearRules::MaxArmorSubclass(CLASS_DRUID, level) == ITEM_SUBCLASS_ARMOR_LEATHER);
        CHECK(lighter(CLASS_DRUID, level, ITEM_SUBCLASS_ARMOR_CLOTH));
        CHECK_FALSE(lighter(CLASS_DRUID, level, ITEM_SUBCLASS_ARMOR_LEATHER));
    }
    // a hunter at 39 has leather as its maximum: cloth is lighter, leather is not; at 40 the maximum is mail
    CHECK(BotGearRules::MaxArmorSubclass(CLASS_HUNTER, 39) == ITEM_SUBCLASS_ARMOR_LEATHER);
    CHECK(lighter(CLASS_HUNTER, 39, ITEM_SUBCLASS_ARMOR_CLOTH));
    CHECK_FALSE(lighter(CLASS_HUNTER, 39, ITEM_SUBCLASS_ARMOR_LEATHER));
    CHECK(BotGearRules::MaxArmorSubclass(CLASS_HUNTER, 40) == ITEM_SUBCLASS_ARMOR_MAIL);
    CHECK(lighter(CLASS_HUNTER, 40, ITEM_SUBCLASS_ARMOR_LEATHER));
    CHECK_FALSE(lighter(CLASS_HUNTER, 40, ITEM_SUBCLASS_ARMOR_MAIL));
    // a shaman at 39 has leather as its maximum (cloth is lighter); at 40 mail
    CHECK(BotGearRules::MaxArmorSubclass(CLASS_SHAMAN, 39) == ITEM_SUBCLASS_ARMOR_LEATHER);
    CHECK(lighter(CLASS_SHAMAN, 39, ITEM_SUBCLASS_ARMOR_CLOTH));
    CHECK_FALSE(lighter(CLASS_SHAMAN, 39, ITEM_SUBCLASS_ARMOR_LEATHER));
    CHECK_FALSE(lighter(CLASS_SHAMAN, 39, ITEM_SUBCLASS_ARMOR_MAIL)); // too heavy, not too light
    CHECK(BotGearRules::MaxArmorSubclass(CLASS_SHAMAN, 40) == ITEM_SUBCLASS_ARMOR_MAIL);
    CHECK(lighter(CLASS_SHAMAN, 40, ITEM_SUBCLASS_ARMOR_LEATHER));
    CHECK_FALSE(lighter(CLASS_SHAMAN, 40, ITEM_SUBCLASS_ARMOR_MAIL));
    // a cloth class never has anything lighter
    for (uint32 level : { 1u, 39u, 60u })
        CHECK_FALSE(lighter(CLASS_MAGE, level, ITEM_SUBCLASS_ARMOR_CLOTH));
    // the item level rule alone decides between two pieces of the same weight: an equal or lower item level keeps the worn piece
    CHECK(BotGearRules::KeepWorn(20, 20, false, false));
    CHECK(BotGearRules::KeepWorn(20, 15, false, false));
    CHECK_FALSE(BotGearRules::KeepWorn(20, 21, false, false));
    // and the heaviest armour a class may wear is unchanged
    CHECK(BotGearRules::ArmorTooHeavy(CLASS_SHAMAN, 39, ITEM_CLASS_ARMOR, ITEM_SUBCLASS_ARMOR_MAIL, INVTYPE_CHEST));
}

TEST_CASE("A worn chest of lighter armour is replaced when a maximum-subclass candidate exists", "[BotGearRules]")
{
    // worn cloth (lighter), candidate mail (not lighter): replaced at an equal, a higher and a lower item level
    CHECK_FALSE(BotGearRules::KeepWorn(30, 30, true, false));
    CHECK_FALSE(BotGearRules::KeepWorn(45, 30, true, false));
    CHECK_FALSE(BotGearRules::KeepWorn(30, 45, true, false));
    // worn mail stays against cloth, even a higher cloth item level
    CHECK(BotGearRules::KeepWorn(30, 45, false, true));
    // both lighter (no candidate of the right subclass): the item level rule decides, the fallback
    CHECK(BotGearRules::KeepWorn(30, 30, true, true));
    CHECK_FALSE(BotGearRules::KeepWorn(30, 31, true, true));
}

TEST_CASE("The slot pick prefers the class maximum armour", "[BotGearRules]")
{
    using BotGearRules::SlotCandidate;
    // shaman at 40+: the cloth chest scores higher, the mail chest is chosen
    std::vector<SlotCandidate> shaman = { { 1, 500, BotGearRules::ArmorLighterThanMax(CLASS_SHAMAN, 45, ITEM_CLASS_ARMOR, ITEM_SUBCLASS_ARMOR_CLOTH, INVTYPE_CHEST) },
                                          { 2, 300, BotGearRules::ArmorLighterThanMax(CLASS_SHAMAN, 45, ITEM_CLASS_ARMOR, ITEM_SUBCLASS_ARMOR_MAIL, INVTYPE_CHEST) } };
    CHECK(BotGearRules::ChooseForSlot(shaman, 1) == std::vector<uint32>{ 2 });
    // druid: leather over cloth
    std::vector<SlotCandidate> druid = { { 3, 500, BotGearRules::ArmorLighterThanMax(CLASS_DRUID, 45, ITEM_CLASS_ARMOR, ITEM_SUBCLASS_ARMOR_CLOTH, INVTYPE_CHEST) },
                                         { 4, 300, BotGearRules::ArmorLighterThanMax(CLASS_DRUID, 45, ITEM_CLASS_ARMOR, ITEM_SUBCLASS_ARMOR_LEATHER, INVTYPE_CHEST) } };
    CHECK(BotGearRules::ChooseForSlot(druid, 1) == std::vector<uint32>{ 4 });
    // hunter at 40+: mail over leather
    std::vector<SlotCandidate> hunter = { { 5, 500, BotGearRules::ArmorLighterThanMax(CLASS_HUNTER, 40, ITEM_CLASS_ARMOR, ITEM_SUBCLASS_ARMOR_LEATHER, INVTYPE_LEGS) },
                                          { 6, 300, BotGearRules::ArmorLighterThanMax(CLASS_HUNTER, 40, ITEM_CLASS_ARMOR, ITEM_SUBCLASS_ARMOR_MAIL, INVTYPE_LEGS) } };
    CHECK(BotGearRules::ChooseForSlot(hunter, 1) == std::vector<uint32>{ 6 });
    // no maximum-subclass candidate: the best lighter one stays as the fallback
    std::vector<SlotCandidate> fallback = { { 7, 200, true }, { 8, 400, true } };
    CHECK(BotGearRules::ChooseForSlot(fallback, 1) == std::vector<uint32>{ 8 });
    // a hunter at 39: leather is its maximum, so it beats the higher-scoring cloth
    std::vector<SlotCandidate> leveling = { { 9, 500, BotGearRules::ArmorLighterThanMax(CLASS_HUNTER, 39, ITEM_CLASS_ARMOR, ITEM_SUBCLASS_ARMOR_CLOTH, INVTYPE_CHEST) },
                                            { 10, 300, BotGearRules::ArmorLighterThanMax(CLASS_HUNTER, 39, ITEM_CLASS_ARMOR, ITEM_SUBCLASS_ARMOR_LEATHER, INVTYPE_CHEST) } };
    CHECK(BotGearRules::ChooseForSlot(leveling, 1) == std::vector<uint32>{ 10 });
    // a druid at level 1: leather over cloth
    std::vector<SlotCandidate> druid1 = { { 13, 500, BotGearRules::ArmorLighterThanMax(CLASS_DRUID, 1, ITEM_CLASS_ARMOR, ITEM_SUBCLASS_ARMOR_CLOTH, INVTYPE_CHEST) },
                                          { 14, 100, BotGearRules::ArmorLighterThanMax(CLASS_DRUID, 1, ITEM_CLASS_ARMOR, ITEM_SUBCLASS_ARMOR_LEATHER, INVTYPE_CHEST) } };
    CHECK(BotGearRules::ChooseForSlot(druid1, 1) == std::vector<uint32>{ 14 });
    // a shaman at 39 takes leather over cloth, at 40 mail over leather
    std::vector<SlotCandidate> shaman39 = { { 15, 500, BotGearRules::ArmorLighterThanMax(CLASS_SHAMAN, 39, ITEM_CLASS_ARMOR, ITEM_SUBCLASS_ARMOR_CLOTH, INVTYPE_HEAD) },
                                            { 16, 100, BotGearRules::ArmorLighterThanMax(CLASS_SHAMAN, 39, ITEM_CLASS_ARMOR, ITEM_SUBCLASS_ARMOR_LEATHER, INVTYPE_HEAD) } };
    CHECK(BotGearRules::ChooseForSlot(shaman39, 1) == std::vector<uint32>{ 16 });
    std::vector<SlotCandidate> shaman40 = { { 17, 500, BotGearRules::ArmorLighterThanMax(CLASS_SHAMAN, 40, ITEM_CLASS_ARMOR, ITEM_SUBCLASS_ARMOR_LEATHER, INVTYPE_HEAD) },
                                            { 18, 100, BotGearRules::ArmorLighterThanMax(CLASS_SHAMAN, 40, ITEM_CLASS_ARMOR, ITEM_SUBCLASS_ARMOR_MAIL, INVTYPE_HEAD) } };
    CHECK(BotGearRules::ChooseForSlot(shaman40, 1) == std::vector<uint32>{ 18 });
    // a tie keeps the earlier candidate; no candidates, no pick
    CHECK(BotGearRules::ChooseForSlot({ { 11, 100, false }, { 12, 100, false } }, 1) == std::vector<uint32>{ 11 });
    CHECK(BotGearRules::ChooseForSlot({}, 1).empty());
}

TEST_CASE("A second ring and a second trinket are chosen", "[BotGearRules]")
{
    using BotGearRules::SlotCandidate;
    // the two best distinct rings, best first: they go in finger 1 and finger 2
    CHECK(BotGearRules::ChooseForSlot({ { 20, 100, false }, { 21, 300, false }, { 22, 200, false } }, 2) == std::vector<uint32>{ 21, 22 });
    // the same for trinkets
    CHECK(BotGearRules::ChooseForSlot({ { 30, 50, false }, { 31, 80, false } }, 2) == std::vector<uint32>{ 31, 30 });
    // an item listed twice (a unique-equipped ring) is chosen once
    CHECK(BotGearRules::ChooseForSlot({ { 40, 300, false }, { 40, 300, false }, { 41, 100, false } }, 2) == std::vector<uint32>{ 40, 41 });
    // a single candidate fills one slot only
    CHECK(BotGearRules::ChooseForSlot({ { 50, 100, false } }, 2) == std::vector<uint32>{ 50 });
}

TEST_CASE("A unique-equipped ring is chosen once and a ranged weapon fills slot 17", "[BotGearRules]")
{
    using BotGearRules::SlotCandidate;
    // the same unique-equipped ring offered twice, plus a weaker different ring: the second slot gets the different ring, not a copy
    CHECK(BotGearRules::ChooseForSlot({ { 60, 400, false }, { 60, 400, false }, { 61, 50, false } }, 2) == std::vector<uint32>{ 60, 61 });
    // only the unique ring exists: one pick, the second slot stays empty
    CHECK(BotGearRules::ChooseForSlot({ { 60, 400, false }, { 60, 400, false } }, 2) == std::vector<uint32>{ 60 });
    // a hunter's slot 17: the best usable bow, gun or crossbow wins (jewellery and weapons are never "lighter")
    bool const bowLighter = BotGearRules::ArmorLighterThanMax(CLASS_HUNTER, 30, ITEM_CLASS_WEAPON, ITEM_SUBCLASS_WEAPON_BOW, INVTYPE_RANGED);
    CHECK_FALSE(bowLighter);
    CHECK(BotGearRules::ChooseForSlot({ { 70, 120, bowLighter }, { 71, 180, bowLighter }, { 72, 90, bowLighter } }, 1) == std::vector<uint32>{ 71 });
    // no usable ranged candidate (a class without the skill): nothing is picked, the slot stays empty
    CHECK(BotGearRules::ChooseForSlot({}, 1).empty());
}

TEST_CASE("KeepWorn compares gear scores, so a lower item level with the wanted primary stat can replace the worn piece", "[BotGearRules]")
{
    // item level 20 worn with no wanted stats (200) against item level 19 with 5 of a wanted primary stat (190 + 15): replaced
    CHECK_FALSE(BotGearRules::KeepWorn(200, 205, false, false));
    // equal scores keep the worn piece
    CHECK(BotGearRules::KeepWorn(205, 205, false, false));
}

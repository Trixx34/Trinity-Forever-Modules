#include "tc_catch2.h"
#include "ModuleBotGearSellRules.h"

#include <string>

using namespace ModuleBotGearSell;

namespace
{
GearItem Bag(int32 score, uint32 level = 20)
{
    GearItem item;
    item.ItemLevel = level;
    item.Quality = 2;
    item.RequiredLevel = 15;
    item.InventoryType = INVTYPE_CHEST;
    item.ArmorSubclass = 2;
    item.Score = score;
    return item;
}

GearItem Worn(int32 score, uint32 level = 30)
{
    GearItem item = Bag(score, level);
    item.Quality = 3;
    return item;
}
}

TEST_CASE("The sell bit is 2048, free of the older vendor bits, and the profile sits above it", "[ModuleBotGearSellRules]")
{
    CHECK(kServiceSellGearBit == 2048);
    CHECK((kServiceSellGearBit & 1023) == 0);                 // bits 1..512 are the older services, 1024 is white junk
    CHECK((kServiceSellGearBit & (7u << kGearProfileShift)) == 0);
    CHECK(GearProfileOf(kServiceSellGearBit) == 0);
    CHECK(GearProfileOf(kServiceSellGearBit | (3u << kGearProfileShift)) == 3);
    CHECK(GearProfileOf(1 | 2 | 4 | 1024 | kServiceSellGearBit | (6u << kGearProfileShift)) == 6);
}

TEST_CASE("Bag gear is sold only when every worn slot of its type outclasses it", "[ModuleBotGearSellRules]")
{
    GearItem const worn = Worn(400);
    CHECK(JudgeBagGear(30, Bag(300), &worn, 1, 1) == Verdict::Sell);
    CHECK(JudgeBagGear(30, Bag(371), &worn, 1, 1) == Verdict::KeepCouldBeBetter);   // 29 behind: inside the margin
    CHECK(JudgeBagGear(30, Bag(370), &worn, 1, 1) == Verdict::Sell);                // exactly the margin
    CHECK(JudgeBagGear(30, Bag(300, 31), &worn, 1, 1) == Verdict::KeepCouldBeBetter); // a higher item level than worn
}

TEST_CASE("Two-slot types need both slots worn and both outclassing", "[ModuleBotGearSellRules]")
{
    GearItem pair[2] = { Worn(400), Worn(200) };
    CHECK(JudgeBagGear(30, Bag(250), pair, 2, 2) == Verdict::KeepCouldBeBetter);     // beats the weaker ring
    CHECK(JudgeBagGear(30, Bag(100), pair, 2, 2) == Verdict::Sell);
    CHECK(JudgeBagGear(30, Bag(100), pair, 1, 2) == Verdict::KeepNothingWorn);       // one ring slot is empty
    CHECK(JudgeBagGear(30, Bag(100), pair, 0, 1) == Verdict::KeepNothingWorn);
    CHECK(JudgeBagGear(30, Bag(100), pair, 1, 0) == Verdict::KeepNothingWorn);
}

TEST_CASE("Quest, set, blue, unusable, later-level and non-equipment items are never sold", "[ModuleBotGearSellRules]")
{
    GearItem const worn = Worn(1000);
    GearItem quest = Bag(1); quest.QuestItem = true;
    GearItem set = Bag(1); set.SetItem = true;
    GearItem blue = Bag(1); blue.Quality = 3;
    GearItem green = Bag(1); green.Quality = 2;
    GearItem grey = Bag(1); grey.Quality = 0;
    GearItem unusable = Bag(1); unusable.Usable = false;
    GearItem later = Bag(1); later.RequiredLevel = 31;
    GearItem junk = Bag(1); junk.InventoryType = 0;
    CHECK(JudgeBagGear(30, quest, &worn, 1, 1) == Verdict::KeepQuestItem);
    CHECK(JudgeBagGear(30, set, &worn, 1, 1) == Verdict::KeepSetItem);
    CHECK(JudgeBagGear(30, blue, &worn, 1, 1) == Verdict::KeepQuality);
    CHECK(JudgeBagGear(30, green, &worn, 1, 1) == Verdict::Sell);
    CHECK(JudgeBagGear(30, grey, &worn, 1, 1) == Verdict::Sell);
    CHECK(JudgeBagGear(30, unusable, &worn, 1, 1) == Verdict::KeepNotUsable);
    CHECK(JudgeBagGear(30, later, &worn, 1, 1) == Verdict::KeepLevelLater);
    CHECK(JudgeBagGear(31, later, &worn, 1, 1) == Verdict::Sell);
    CHECK(JudgeBagGear(30, junk, &worn, 1, 1) == Verdict::KeepNotEquipment);
}

TEST_CASE("Gear of another slot type or heavier armour than worn is kept", "[ModuleBotGearSellRules]")
{
    GearItem otherType = Worn(1000);
    otherType.InventoryType = INVTYPE_ROBE;
    CHECK(JudgeBagGear(30, Bag(1), &otherType, 1, 1) == Verdict::KeepOtherSlotType);
    GearItem lighter = Worn(1000);
    lighter.ArmorSubclass = 1;
    CHECK(JudgeBagGear(30, Bag(1), &lighter, 1, 1) == Verdict::KeepHeavierArmor);
}

TEST_CASE("Items are judged by their numbers, never by their id", "[ModuleBotGearSellRules]")
{
    // GearItem carries no id at all: an item the world adds later with a high id is judged like any other
    CHECK(std::string(VerdictName(Verdict::Sell)) == "outclassed");
    CHECK(std::string(VerdictName(Verdict::KeepQuestItem)) == "keep_quest_item");
}

TEST_CASE("Role extras add attack power, spell power and hit by the profile", "[ModuleBotGearSellRules]")
{
    CHECK(RoleStatExtra(0, ITEM_MOD_ATTACK_POWER, 10) == 10);
    CHECK(RoleStatExtra(1, ITEM_MOD_ATTACK_POWER, 10) == 10);
    CHECK(RoleStatExtra(5, ITEM_MOD_ATTACK_POWER, 10) == 0);   // a tank profile
    CHECK(RoleStatExtra(2, ITEM_MOD_ATTACK_POWER, 10) == 0);   // a caster
    CHECK(RoleStatExtra(3, ITEM_MOD_SPELL_POWER, 7) == 7);
    CHECK(RoleStatExtra(0, ITEM_MOD_SPELL_POWER, 7) == 0);
    CHECK(RoleStatExtra(4, ITEM_MOD_HIT_RATING, 4) == 4);
    CHECK(RoleStatExtra(6, ITEM_MOD_HIT_RATING, 4) == 0);
    CHECK(RoleStatExtra(0, ITEM_MOD_STAMINA, 9) == 0);
}

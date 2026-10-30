#include "tc_catch2.h"
#include "ModuleBotSupplyRules.h"

#include <limits>
#include <string>

using namespace ModuleBotSupplyRules;

namespace
{
bool Junk(uint32 quality, uint32 itemClass, uint32 bonding = BIND_NONE, uint32 startQuest = 0, bool equippedBag = false)
{
    return IsWhiteJunk(quality, bonding, startQuest, equippedBag, itemClass);
}
}

TEST_CASE("White junk is only common trade goods, bags and miscellaneous items", "[ModuleBotSupplyRules]")
{
    // the item classes a bot sells to empty a full bag (kVendor bit 1024, 2026-10-20)
    CHECK(Junk(kQualityCommon, ITEM_CLASS_TRADE_GOODS));
    CHECK(Junk(kQualityCommon, ITEM_CLASS_CONTAINER));
    CHECK(Junk(kQualityCommon, ITEM_CLASS_MISCELLANEOUS));

    // everything the bot eats, shoots, casts with, wears or wields has its own rules
    for (uint32 itemClass : { uint32(ITEM_CLASS_CONSUMABLE), uint32(ITEM_CLASS_WEAPON), uint32(ITEM_CLASS_GEM), uint32(ITEM_CLASS_ARMOR),
        uint32(ITEM_CLASS_REAGENT), uint32(ITEM_CLASS_PROJECTILE), uint32(ITEM_CLASS_RECIPE), uint32(ITEM_CLASS_QUIVER),
        uint32(ITEM_CLASS_QUEST), uint32(ITEM_CLASS_KEY) })
        CHECK_FALSE(Junk(kQualityCommon, itemClass));
}

TEST_CASE("White junk excludes every quality but common", "[ModuleBotSupplyRules]")
{
    CHECK_FALSE(Junk(kQualityPoor, ITEM_CLASS_TRADE_GOODS)); // grey items are sold by their own rule
    CHECK(Junk(kQualityCommon, ITEM_CLASS_TRADE_GOODS));
    CHECK_FALSE(Junk(2, ITEM_CLASS_TRADE_GOODS));            // uncommon
    CHECK_FALSE(Junk(3, ITEM_CLASS_CONTAINER));              // rare
    CHECK_FALSE(Junk(5, ITEM_CLASS_MISCELLANEOUS));          // legendary
}

TEST_CASE("White junk never sells quest items, quest starters or an equipped bag", "[ModuleBotSupplyRules]")
{
    CHECK_FALSE(Junk(kQualityCommon, ITEM_CLASS_MISCELLANEOUS, BIND_QUEST));
    CHECK(Junk(kQualityCommon, ITEM_CLASS_MISCELLANEOUS, BIND_ON_ACQUIRE));
    CHECK(Junk(kQualityCommon, ITEM_CLASS_MISCELLANEOUS, BIND_ON_EQUIP));
    CHECK(Junk(kQualityCommon, ITEM_CLASS_MISCELLANEOUS, BIND_ON_USE));

    CHECK_FALSE(Junk(kQualityCommon, ITEM_CLASS_MISCELLANEOUS, BIND_NONE, 1));
    CHECK_FALSE(Junk(kQualityCommon, ITEM_CLASS_TRADE_GOODS, BIND_NONE, std::numeric_limits<uint32>::max()));

    // a bag worn in a bag slot is not junk, a spare one in the backpack or another bag is
    CHECK_FALSE(Junk(kQualityCommon, ITEM_CLASS_CONTAINER, BIND_NONE, 0, true));
    CHECK(Junk(kQualityCommon, ITEM_CLASS_CONTAINER, BIND_NONE, 0, false));
}

TEST_CASE("The junk sale keeps 60 pieces of each trade good and sells the surplus", "[ModuleBotSupplyRules]")
{
    CHECK(kJunkKeepTradeGoods == 60);

    // three stacks of 20 are kept, the fourth is the surplus
    uint32 left = kJunkKeepTradeGoods;
    CHECK(KeepTradeGoodStack(left, 20));
    CHECK(left == 40);
    CHECK(KeepTradeGoodStack(left, 20));
    CHECK(left == 20);
    CHECK(KeepTradeGoodStack(left, 20));
    CHECK(left == 0);
    CHECK_FALSE(KeepTradeGoodStack(left, 20));
    CHECK(left == 0);
}

TEST_CASE("The junk sale keeps a whole stack until the total reaches the keep count", "[ModuleBotSupplyRules]")
{
    uint32 left = kJunkKeepTradeGoods;
    CHECK(KeepTradeGoodStack(left, 20));    // 40 left to keep
    CHECK(KeepTradeGoodStack(left, 100));   // a 100 stack straddling the limit is kept whole, not split
    CHECK(left == 0);
    CHECK_FALSE(KeepTradeGoodStack(left, 1));

    uint32 exact = 60;
    CHECK(KeepTradeGoodStack(exact, 60));   // exactly the keep count
    CHECK(exact == 0);
    CHECK_FALSE(KeepTradeGoodStack(exact, 5));

    uint32 none = 0;                         // nothing to keep: sold, `left` untouched
    CHECK_FALSE(KeepTradeGoodStack(none, 1));
    CHECK(none == 0);
}

TEST_CASE("Autogear flags carry tank, bag-only and a three-bit gear profile", "[ModuleBotSupplyRules]")
{
    // flags 0 is also what a module build that predates the profile sends
    CHECK_FALSE(AutoGearIsTank(0));
    CHECK_FALSE(AutoGearIsBagOnly(0));
    CHECK(AutoGearProfileOf(0) == 0);

    CHECK(AutoGearIsTank(1));
    CHECK_FALSE(AutoGearIsBagOnly(1));
    CHECK(AutoGearProfileOf(1) == 0);

    CHECK_FALSE(AutoGearIsTank(2));
    CHECK(AutoGearIsBagOnly(2));
    CHECK(AutoGearProfileOf(2) == 0);

    // profile n sits in bits 2-4: 4 * n
    for (uint32 profile = 0; profile <= 7; ++profile)
    {
        CHECK(AutoGearProfileOf(profile << 2) == profile);
        CHECK(AutoGearProfileOf((profile << 2) | 3) == profile);
        CHECK(AutoGearIsTank((profile << 2) | 1));
    }
    CHECK(AutoGearProfileOf(20 | 1) == 5);   // protection warrior/paladin: StrStamDef plus the tank bit
    CHECK(AutoGearProfileOf(24 | 1) == 6);   // feral tank druid: AgiStamDef plus the tank bit

    CHECK(AutoGearProfileOf(32) == 0);       // bit 5 is not part of the profile
    CHECK(AutoGearProfileOf(std::numeric_limits<uint32>::max()) == 7);
}

TEST_CASE("Unfinished-looking item names are not handed out", "[ModuleBotSupplyRules]")
{
    CHECK(IsPlaceholderItemName(""));
    CHECK(IsPlaceholderItemName("deprecated sword of nothing"));
    CHECK(IsPlaceholderItemName("sword (deprecated)"));
    CHECK(IsPlaceholderItemName("test sword"));
    CHECK(IsPlaceholderItemName("unused helm"));
    CHECK(IsPlaceholderItemName("cloak [ph]"));
    CHECK(IsPlaceholderItemName("zz old boots"));

    CHECK_FALSE(IsPlaceholderItemName("linen vest"));
    CHECK_FALSE(IsPlaceholderItemName("the greatest axe")); // "test" only counts at the start
    CHECK_FALSE(IsPlaceholderItemName("lazzy hat"));        // "zz" only counts at the start
}

TEST_CASE("Gear score counts wanted stats, penalises unused primary stats and ignores other stats", "[ModuleBotSupplyRules]")
{
    CHECK(StatScore(true, true, 7) == 7);
    CHECK(StatScore(true, false, 7) == 7);       // a wanted secondary stat
    CHECK(StatScore(false, true, 7) == -21);     // a primary stat the class does not use counts against the item
    CHECK(StatScore(false, false, 7) == 0);      // any other stat
    CHECK(StatScore(false, true, 0) == 0);

    CHECK(CombineGearScore(20, 0) == 200);
    CHECK(CombineGearScore(20, 15) == 215);
    CHECK(CombineGearScore(20, -21) == 179);
    CHECK(CombineGearScore(0, 0) == 0);

    // the item level dominates: 9 points of stats do not beat one item level
    CHECK(CombineGearScore(21, 0) > CombineGearScore(20, 9));
    // ... but a wrong primary stat can lose it: 21 with 4 unwanted strength vs 20 clean
    CHECK(CombineGearScore(21, StatScore(false, true, 4)) < CombineGearScore(20, 0));
}

TEST_CASE("A gear pick is replaced only by a strictly better score", "[ModuleBotSupplyRules]")
{
    CHECK(ShouldReplacePick(false, std::numeric_limits<int32>::min(), std::numeric_limits<int32>::min())); // the first candidate always wins
    CHECK(ShouldReplacePick(false, 100, -500));
    CHECK(ShouldReplacePick(true, 100, 101));
    CHECK_FALSE(ShouldReplacePick(true, 100, 100)); // a tie keeps the earlier pick
    CHECK_FALSE(ShouldReplacePick(true, 100, 99));
    CHECK(ShouldReplacePick(true, -10, -9));
}

TEST_CASE("A worn item is replaced only by a higher item level", "[ModuleBotSupplyRules]")
{
    CHECK(IsItemLevelUpgrade(10, 11));
    CHECK_FALSE(IsItemLevelUpgrade(10, 10));  // equal is kept, so the two gear passes cannot undo each other
    CHECK_FALSE(IsItemLevelUpgrade(10, 9));
    CHECK(IsItemLevelUpgrade(0, 1));
    CHECK_FALSE(IsItemLevelUpgrade(0, 0));

    CHECK(PositiveGain(5) == 5);
    CHECK(PositiveGain(1) == 1);
    CHECK(PositiveGain(0) == 0);
    CHECK(PositiveGain(-5) == 0);
}

TEST_CASE("Consume spell_id selects the kind of supply, anything else is refused", "[ModuleBotSupplyRules]")
{
    CHECK(ConsumeKindFor(1) == Kind::Food);
    CHECK(ConsumeKindFor(2) == Kind::Drink);
    CHECK(ConsumeKindFor(3) == Kind::HealthPotion);
    CHECK(ConsumeKindFor(4) == Kind::ManaPotion);
    CHECK(ConsumeKindFor(5) == Kind::Bandage);

    CHECK(ConsumeKindFor(0) == Kind::None);
    CHECK(ConsumeKindFor(6) == Kind::None);
    CHECK(ConsumeKindFor(256 + 1) == Kind::None);
    CHECK(ConsumeKindFor(std::numeric_limits<uint32>::max()) == Kind::None);
}

TEST_CASE("Only potions are used in combat and on the move", "[ModuleBotSupplyRules]")
{
    CHECK_FALSE(IsPotionKind(Kind::None));
    CHECK_FALSE(IsPotionKind(Kind::Food));
    CHECK_FALSE(IsPotionKind(Kind::Drink));
    CHECK(IsPotionKind(Kind::HealthPotion));
    CHECK(IsPotionKind(Kind::ManaPotion));
    CHECK_FALSE(IsPotionKind(Kind::Bandage));

    // the selection order of spell_id 1..5 is food, drink, health potion, mana potion, bandage
    CHECK_FALSE(IsPotionKind(ConsumeKindFor(1)));
    CHECK_FALSE(IsPotionKind(ConsumeKindFor(2)));
    CHECK(IsPotionKind(ConsumeKindFor(3)));
    CHECK(IsPotionKind(ConsumeKindFor(4)));
    CHECK_FALSE(IsPotionKind(ConsumeKindFor(5)));
}

TEST_CASE("Drinks and mana potions need a mana user, everything else fits every class", "[ModuleBotSupplyRules]")
{
    CHECK(KindFitsClass(Kind::Food, false));
    CHECK(KindFitsClass(Kind::Food, true));
    CHECK_FALSE(KindFitsClass(Kind::Drink, false));
    CHECK(KindFitsClass(Kind::Drink, true));
    CHECK(KindFitsClass(Kind::HealthPotion, false));
    CHECK(KindFitsClass(Kind::HealthPotion, true));
    CHECK_FALSE(KindFitsClass(Kind::ManaPotion, false));
    CHECK(KindFitsClass(Kind::ManaPotion, true));
    CHECK(KindFitsClass(Kind::Bandage, false));
    CHECK(KindFitsClass(Kind::Bandage, true));
    CHECK(KindFitsClass(Kind::None, false));
}

TEST_CASE("The money reserve is perLevel copper for each level and only the rest is spendable", "[ModuleBotSupplyRules]")
{
    CHECK(ReserveFor(60, 5) == 300);
    CHECK(ReserveFor(1, 0) == 0);
    CHECK(ReserveFor(0, 5) == 0);
    CHECK(ReserveFor(std::numeric_limits<uint32>::max(), std::numeric_limits<uint32>::max()) ==
        uint64(std::numeric_limits<uint32>::max()) * std::numeric_limits<uint32>::max()); // no 32-bit overflow

    CHECK(Spendable(299, 300) == 0);   // just below the reserve
    CHECK(Spendable(300, 300) == 0);   // at the reserve
    CHECK(Spendable(301, 300) == 1);   // just above
    CHECK(Spendable(1000, 300) == 700);
    CHECK(Spendable(0, 0) == 0);
    CHECK(Spendable(5, 0) == 5);
}

TEST_CASE("A supply target is topped up by the difference to what is carried", "[ModuleBotSupplyRules]")
{
    CHECK(NeedToBuy(20, 0) == 20);
    CHECK(NeedToBuy(20, 19) == 1);   // just below the target
    CHECK(NeedToBuy(20, 20) == 0);   // at the target
    CHECK(NeedToBuy(20, 21) == 0);   // above: no unsigned wrap-around
    CHECK(NeedToBuy(0, 0) == 0);
    CHECK(NeedToBuy(0, 5) == 0);
}

TEST_CASE("The price per piece rounds up per stack of buyCount", "[ModuleBotSupplyRules]")
{
    CHECK(PerPieceCopper(100, 20) == 5);
    CHECK(PerPieceCopper(101, 20) == 6);   // rounded up: on the safe side
    CHECK(PerPieceCopper(99, 20) == 5);
    CHECK(PerPieceCopper(7, 1) == 7);
    CHECK(PerPieceCopper(0, 1) == 0);      // free items
    CHECK(PerPieceCopper(5, 0) == 5);      // never divides by zero
    CHECK(PerPieceCopper(std::numeric_limits<uint32>::max(), 1) == std::numeric_limits<uint32>::max());
}

TEST_CASE("The number to buy is limited by the spendable money", "[ModuleBotSupplyRules]")
{
    // 10 wanted at 5 copper each: 50 copper pays for all of them
    CHECK(AffordableCount(10, 49, 5) == 9);    // just below
    CHECK(AffordableCount(10, 50, 5) == 10);   // at
    CHECK(AffordableCount(10, 55, 5) == 10);   // above: never more than wanted
    CHECK(AffordableCount(10, 4, 5) == 0);
    CHECK(AffordableCount(10, 0, 5) == 0);
    CHECK(AffordableCount(0, 100, 5) == 0);
    CHECK(AffordableCount(10, 0, 0) == 10);    // a free item is not limited by money
    CHECK(AffordableCount(10, std::numeric_limits<uint64>::max(), 1) == 10);
}

TEST_CASE("A refused purchase is retried a few times with three quarters of the count", "[ModuleBotSupplyRules]")
{
    CHECK(kBuyAttempts == 3);
    CHECK(ShrinkBuyCount(20) == 15);
    CHECK(ShrinkBuyCount(15) == 11);
    CHECK(ShrinkBuyCount(10) == 7);
    CHECK(ShrinkBuyCount(7) == 5);
    CHECK(ShrinkBuyCount(5) == 3);
    CHECK(ShrinkBuyCount(3) == 2);
    CHECK(ShrinkBuyCount(2) == 1);
    CHECK(ShrinkBuyCount(1) == 0);   // the loop ends when the count reaches 0
    CHECK(ShrinkBuyCount(0) == 0);
}

TEST_CASE("A purchase is cut to the number of pieces the bags have room for", "[ModuleBotSupplyRules]")
{
    uint32 calls = 0;
    auto const room = [&calls](uint32 capacity)
    {
        return [&calls, capacity](uint32 pieces) { ++calls; return pieces <= capacity; };
    };

    CHECK(LargestFitting(5, room(5)) == 5);
    CHECK(calls == 1);                       // everything fits: one question
    calls = 0;
    CHECK(LargestFitting(5, room(100)) == 5);
    CHECK(calls == 1);

    CHECK(LargestFitting(5, room(0)) == 0);  // full bags: nothing fits
    CHECK(LargestFitting(5, room(1)) == 1);
    CHECK(LargestFitting(5, room(3)) == 3);
    CHECK(LargestFitting(5, room(4)) == 4);
    CHECK(LargestFitting(0, room(0)) == 0);
    CHECK(LargestFitting(0, room(7)) == 0);

    // every capacity of a 200 piece purchase, and the question is never asked for more than the count
    uint32 largestAsked = 0;
    for (uint32 capacity = 0; capacity <= 210; ++capacity)
    {
        largestAsked = 0;
        uint32 const fitted = LargestFitting(200, [&](uint32 pieces) { largestAsked = pieces > largestAsked ? pieces : largestAsked; return pieces <= capacity; });
        CHECK(fitted == (capacity < 200 ? capacity : 200));
        CHECK(largestAsked <= 200);
    }
}

TEST_CASE("The best vendor item is the highest level, then the dearest, then the lowest entry", "[ModuleBotSupplyRules]")
{
    CHECK(IsBetterVendorPick(25, 10, 500, 5, 900, 1));    // the level wins over price and entry
    CHECK_FALSE(IsBetterVendorPick(5, 900, 1, 25, 10, 500));

    CHECK(IsBetterVendorPick(25, 11, 500, 25, 10, 1));    // same level: the dearer one
    CHECK_FALSE(IsBetterVendorPick(25, 10, 1, 25, 11, 500));

    CHECK(IsBetterVendorPick(25, 10, 499, 25, 10, 500));  // same level and price: the lower entry, so the choice is stable
    CHECK_FALSE(IsBetterVendorPick(25, 10, 500, 25, 10, 500)); // the same item is not better than itself
    CHECK_FALSE(IsBetterVendorPick(25, 10, 501, 25, 10, 500));
    CHECK(IsBetterVendorPick(0, 0, 1, -1, 0, 1));         // a negative required level compares as a plain integer
}

TEST_CASE("The best carried food is the highest level, then the smaller stack", "[ModuleBotSupplyRules]")
{
    CHECK(IsBetterCarried(35, 20, 25, 1));          // a higher level is eaten first, whatever the stacks
    CHECK_FALSE(IsBetterCarried(25, 1, 35, 20));

    CHECK(IsBetterCarried(25, 4, 25, 5));           // same level: the smaller stack goes first so stacks do not linger
    CHECK_FALSE(IsBetterCarried(25, 5, 25, 5));     // a tie keeps the first found
    CHECK_FALSE(IsBetterCarried(25, 6, 25, 5));
}

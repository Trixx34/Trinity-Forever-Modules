#include "tc_catch2.h"
#include "BotQuestRewardRules.h"

using BotQuestRewardRules::PickReward;
using BotQuestRewardRules::RewardOption;
using BotQuestRewardRules::UpgradeGain;

namespace
{
// A choice the bot can wear in `slot` against a worn item of `wornScore` (KeepWorn: the candidate does not beat it).
RewardOption Worn(uint32 slot, int32 score, int32 wornScore, uint64 sell = 0, bool lighter = false)
{
    RewardOption o;
    o.Wearable = true;
    o.Slot = slot;
    o.Score = score;
    o.WornScore = wornScore;
    o.KeepWorn = score <= wornScore;
    o.Lighter = lighter;
    o.SellPrice = sell;
    return o;
}

// A choice for a slot with nothing worn (or a broken item).
RewardOption Free(uint32 slot, int32 score, uint64 sell = 0, bool lighter = false)
{
    RewardOption o;
    o.Wearable = true;
    o.Slot = slot;
    o.Score = score;
    o.SlotFree = true;
    o.Lighter = lighter;
    o.SellPrice = sell;
    return o;
}

// A choice the bot cannot wear (wrong armour type, no weapon skill, no shield, not equipment at all).
RewardOption Unwearable(uint64 sell)
{
    RewardOption o;
    o.SellPrice = sell;
    return o;
}
}

TEST_CASE("No choices, no pick", "[BotQuestRewardRules]")
{
    CHECK(PickReward({}) == -1);
}

TEST_CASE("The gain of a choice over what is worn", "[BotQuestRewardRules]")
{
    // not wearable: never an upgrade, whatever its score
    RewardOption cloth = Free(4, 300);
    cloth.Wearable = false;
    CHECK(UpgradeGain(cloth) == 0);
    // a free slot gains the whole score, at least 1
    CHECK(UpgradeGain(Free(4, 250)) == 250);
    CHECK(UpgradeGain(Free(4, -20)) == 1);
    // an occupied slot gains the difference; the worn item that stays means no gain
    CHECK(UpgradeGain(Worn(4, 260, 200)) == 60);
    CHECK(UpgradeGain(Worn(4, 200, 200)) == 0);
    CHECK(UpgradeGain(Worn(4, 150, 200)) == 0);
    // the armour weight rule replaces lighter worn armour whatever the scores: an upgrade of at least 1
    RewardOption heavier = Worn(4, 180, 200);
    heavier.KeepWorn = false;
    CHECK(UpgradeGain(heavier) == 1);
}

TEST_CASE("The biggest upgrade wins over a higher sell price", "[BotQuestRewardRules]")
{
    // a mage offered a plate chest (cannot wear it), a cloth robe that beats its worn robe, and cloth boots a little better than its boots
    std::vector<RewardOption> options = { Unwearable(5000), Worn(5, 300, 200, 900), Worn(8, 210, 200, 1200) };
    CHECK(PickReward(options) == 1);
}

TEST_CASE("An empty slot is a big upgrade", "[BotQuestRewardRules]")
{
    // nothing on the wrists beats a modest chest upgrade
    std::vector<RewardOption> options = { Worn(5, 320, 280, 2000), Free(9, 180, 100) };
    CHECK(PickReward(options) == 1);
}

TEST_CASE("A broken worn item counts as an empty slot", "[BotQuestRewardRules]")
{
    // the caller sets SlotFree for a worn item at 0 durability: the same piece then beats a small upgrade elsewhere
    RewardOption legs = Worn(7, 200, 220, 50);
    CHECK(UpgradeGain(legs) == 0);
    legs.SlotFree = true;
    std::vector<RewardOption> options = { Worn(1, 230, 220, 500), legs };
    CHECK(PickReward(options) == 1);
}

TEST_CASE("Without an upgrade the highest sell price wins", "[BotQuestRewardRules]")
{
    // three pieces none of which beats what is worn, and one the bot cannot wear: the best price, wearable or not
    std::vector<RewardOption> options = { Worn(5, 100, 200, 300), Unwearable(800), Worn(8, 150, 200, 500) };
    CHECK(PickReward(options) == 1);
    // nothing wearable at all (a warrior offered three cloth pieces): the highest price
    CHECK(PickReward({ Unwearable(10), Unwearable(40), Unwearable(20) }) == 1);
}

TEST_CASE("Equal gains go to the higher sell price, a full tie to the earlier option", "[BotQuestRewardRules]")
{
    CHECK(PickReward({ Worn(5, 260, 200, 100), Worn(8, 160, 100, 400) }) == 1);
    CHECK(PickReward({ Worn(5, 260, 200, 100), Worn(8, 160, 100, 100) }) == 0);
    CHECK(PickReward({ Unwearable(100), Unwearable(100) }) == 0);
}

TEST_CASE("Lighter armour loses to armour of the class maximum for the same slot", "[BotQuestRewardRules]")
{
    // a shaman at 40 with no chest: a leather chest of a higher score and a mail chest; the mail one is taken (the leather one would be swapped out)
    std::vector<RewardOption> options = { Free(5, 400, 900, true), Free(5, 350, 700, false) };
    CHECK(PickReward(options) == 1);
    // lighter armour is still an upgrade where no maximum-subclass choice is for that slot
    std::vector<RewardOption> otherSlot = { Free(5, 400, 900, true), Worn(10, 230, 200, 700, false) };
    CHECK(PickReward(otherSlot) == 0);
    // and when the maximum-subclass choice for the slot is no upgrade (the worn piece is lighter armour too), the lighter one is
    std::vector<RewardOption> noMaxUpgrade = { Worn(5, 300, 250, 100, true), Worn(5, 200, 250, 700, false) };
    CHECK(PickReward(noMaxUpgrade) == 0);
}

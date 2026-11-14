/*
 * Pure rule for the quest reward a bot chooses when a quest offers a choice of items: no player, item or quest objects, only numbers the
 * caller (ModuleBotActions.cpp PickQuestReward) works out from the bot, the choices and what the bot wears. Never by item id.
 */

#ifndef TRINITY_BOT_QUEST_REWARD_RULES_H
#define TRINITY_BOT_QUEST_REWARD_RULES_H

#include "Define.h"

#include <vector>

namespace BotQuestRewardRules
{
// One item a quest offers as a choice, seen from the bot that hands the quest in.
struct RewardOption
{
    // A weapon or armour piece the bot can wear: it has an equipment slot, the class has the proficiency (armour type, weapon skill, shield),
    // the armour is not heavier than the class wears, and an off-hand weapon only for a class that dual wields. False for anything else
    // (a consumable, a trade good, an item of another class): such an option is never an upgrade, only its sell price counts.
    bool Wearable = false;
    uint32 Slot = 0;          // the equipment slot it would go in (the first of a ring or trinket pair); only read when Wearable
    int32 Score = 0;          // its gear score (GearScore: item level first, then the stats the class wants)
    bool SlotFree = false;    // nothing worn there, or what is worn there is broken (0 durability: it gives nothing until repaired)
    int32 WornScore = 0;      // the gear score of what is worn there (for a two-hander: main hand and off hand together); unread when SlotFree
    bool KeepWorn = false;    // the worn item stays against this one (BotGearRules::KeepWorn: score and armour weight); unread when SlotFree
    bool Lighter = false;     // body armour lighter than the class maximum (BotGearRules::ArmorLighterThanMax)
    uint64 SellPrice = 0;     // vendor sell price of one
};

// How much better the option is than what the bot wears in its slot; 0 = no upgrade. A free slot (empty or broken item) is an upgrade of the
// whole score, at least 1, so even a piece with stats the class does not want beats leaving the slot bare. A worn item that the armour weight
// rule replaces (lighter armour against an item of the class maximum) is an upgrade of at least 1 whatever the scores.
inline int64 UpgradeGain(RewardOption const& option)
{
    if (!option.Wearable)
        return 0;
    if (option.SlotFree)
        return option.Score > 0 ? int64(option.Score) : 1;
    if (option.KeepWorn)
        return 0;
    int64 const gain = int64(option.Score) - int64(option.WornScore);
    return gain > 0 ? gain : 1;
}

// The index (into `options`) of the reward to take, or -1 for no options. The biggest upgrade wins (UpgradeGain); in a slot where some upgrade is
// armour of the class maximum, lighter armour for that slot does not count as an upgrade (the bot would swap it out again at once). Equal gains go
// to the higher sell price. When no option is an upgrade, the highest sell price wins. A full tie keeps the earlier option.
inline int32 PickReward(std::vector<RewardOption> const& options)
{
    std::vector<int64> gains(options.size(), 0);
    for (size_t i = 0; i < options.size(); ++i)
        gains[i] = UpgradeGain(options[i]);

    for (size_t i = 0; i < options.size(); ++i)
    {
        if (gains[i] == 0 || !options[i].Lighter)
            continue;
        for (size_t j = 0; j < options.size(); ++j)
        {
            if (j != i && gains[j] > 0 && !options[j].Lighter && options[j].Slot == options[i].Slot)
            {
                gains[i] = 0;
                break;
            }
        }
    }

    int32 best = -1;
    for (size_t i = 0; i < options.size(); ++i)
    {
        if (best < 0 || gains[i] > gains[best] || (gains[i] == gains[best] && options[i].SellPrice > options[best].SellPrice))
            best = int32(i);
    }
    return best;
}
}

#endif

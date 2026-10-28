/*
 * Applies one bot action a module published through Tier 2 (doc/module-system/03, Tier 2 section).
 *
 * The core only carries out what the module asks, for a bot the module controls. Deciding what a bot does is the
 * module's job. Core checks the owner and that the bot is online (ModuleMgr); this file checks the action itself.
 *
 * Targets are unit references (forever_module::MakeUnitRef): a player by its database guid, or a creature by entry
 * and counter on the bot's map. An attack needs a live target that is hostile to the bot; other actions need only a
 * unit on the bot's map. Summoned creatures are reachable too, since the reference carries the entry and counter.
 */

#ifndef TRINITYCORE_MODULE_BOT_ACTIONS_H
#define TRINITYCORE_MODULE_BOT_ACTIONS_H

#include "Define.h"
#include "ModuleTier2Layout.h"

#include <string>

class ItemTemplate;
class Player;

// Applies the action and returns what happened. Anything but kApplied comes with the reason in `reason` (for the log).
forever_module::BotActionStatus ApplyBotAction(Player& bot, forever_module::BotAction const& action, std::string& reason, uint32 module = 0);

// How much better `item` is for the bot than what it wears where the item goes, by the AutoGear score (item level first, then the stats the bot's class
// uses; the class-only profile), for the auction buy (ModuleActions.cpp). 0 when it is no upgrade the bot would wear: not a weapon or armour, a level or a
// class the bot cannot use it at, armour too heavy, or an item level that does not beat the worn piece (the rule EquipBagUpgrades keeps), so an item with a
// score above 0 is one a bot that owns it wears.
int32 BotGearUpgradeScore(Player& bot, ItemTemplate const& item);

#endif // TRINITYCORE_MODULE_BOT_ACTIONS_H

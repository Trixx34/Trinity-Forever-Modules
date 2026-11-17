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

class Creature;
class ItemTemplate;
class Map;
class Player;

// The live creature a creature unit reference names on `map`: first by the guid the reference spells (a live creature's own counter), then, with
// Bot.Actions.SpawnRefFallback (default 1), by spawn id, because the snapshot's vendor, repairman, banker, trainer, flight master and auctioneer references
// carry the spawn id as the counter while live creatures are numbered by Map::GenerateLowGuid (so they never matched). The fallback only returns a creature
// with the referenced entry and an npc flag (BotActionRules::SpawnRefMatches). Null when there is none.
Creature* ResolveCreatureRef(Map* map, uint64 ref);

// Applies the action and returns what happened. Anything but kApplied comes with the reason in `reason` (for the log). `reasonOut`, when given, receives
// the ActionReason code of a refusal the handler classifies (kQuest accept: ActionResult::reason), else 0.
forever_module::BotActionStatus ApplyBotAction(Player& bot, forever_module::BotAction const& action, std::string& reason, uint32 module = 0,
                                                   uint16* reasonOut = nullptr);

// How much better `item` is for the bot than what it wears where the item goes, by the AutoGear score (item level first, then the stats the bot's class
// uses; the class-only profile), for the auction buy (ModuleActions.cpp). 0 when it is no upgrade the bot would wear: not a weapon or armour, a level or a
// class the bot cannot use it at, a weapon, armour or shield skill it lacks, an off-hand weapon without dual wield, armour too heavy, an item level above the leveling cap (Bot.Gear.MaxItemLevelOverLevel), or an item level that does not beat the worn piece (the rule EquipBagUpgrades keeps), so an item with a
// score above 0 is one a bot that owns it wears.
int32 BotGearUpgradeScore(Player const& bot, ItemTemplate const& item);

#endif // TRINITYCORE_MODULE_BOT_ACTIONS_H

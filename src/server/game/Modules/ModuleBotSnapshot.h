/*
 * Fills the per-bot part of a Tier 2 snapshot from game state (doc/module-system/03, Tier 2 section).
 *
 * Runs on the world thread, once per snapshot tick, for the bots a module controls and that are online. The
 * snapshot carries what a module needs to decide the next action: position, health, mana, combat target and
 * nearby units. Spell cooldowns are not filled yet and read as zero.
 */

#ifndef TRINITYCORE_MODULE_BOT_SNAPSHOT_H
#define TRINITYCORE_MODULE_BOT_SNAPSHOT_H

#include "Define.h"
#include "ModuleTier2.h"
#include "ObjectGuid.h"

#include <vector>

class Player;
class Unit;

// Packs a unit into a unit reference (forever_module::MakeUnitRef). Returns false for a unit that cannot be packed:
// a counter above 32 bits, an entry above 24 bits, or a type other than player or creature.
bool EncodeUnitRef(Unit const& unit, uint64& ref);

// Players the nearby list keeps before creatures. A bot fights creatures; a few players keep following possible. Without
// this cap, a crowd of bots fills every slot with other bots and no creature is ever seen (the 1500-bot run).
constexpr uint32 kNearbyPlayerSlots = 4;

// Whether the bot may attack this unit: the rule behind the snapshot's is_hostile. Hostile units, and neutral creatures that are not
// critters or NPCs; never a unit a player cannot attack. The attack action uses the same rule, so a unit the module is shown as a
// target is one the core lets it attack.
bool BotMayFight(Player const& bot, Unit const& unit);

// What one bot's snapshot needs to remember between ticks. Layout 13's last_target_lost_reason can only be read by a
// module if the core sees the target disappear, so the core has to remember which target the bot had. ModuleMgr keeps one
// of these per bot it owns, next to the nearby cache, and forgets it when it stops owning the bot.
struct BotSnapshotWatch
{
    ObjectGuid LastVictim;     // the victim's guid on the previous snapshot tick (empty when the bot had no target)
    uint32 LostReason = 0;     // why it went: 0 none or not told, 1 killed, 2 leashed, 3 despawned, 4 another player
};

// The snapshot entry for one bot, with up to `maxNearby` units near it. Creatures come first, closest first, and at most
// kNearbyPlayerSlots players are listed ahead of them. When refreshNearby is false the nearby units are the last ones
// stored in cachedNearby, so the range query runs for only some of the bots on each tick. `watch` carries the state of
// the previous tick and is updated by the call.
ModuleSnapshotBot BuildSnapshotBot(Player& bot, uint32 maxNearby, bool refreshNearby,
                                   std::vector<forever_module::NearbyUnit>& cachedNearby, BotSnapshotWatch& watch);

#endif // TRINITYCORE_MODULE_BOT_SNAPSHOT_H

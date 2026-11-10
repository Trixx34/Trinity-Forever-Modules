/*
 * Layout 25: the party view (forever_module::PartyView, PartyMember), one shared block per group of a module's bots, built once per
 * snapshot tick on the world thread and published next to the bot entries (ModuleTier2Region::PublishSnapshot). A bot's snapshot points
 * at its group's view and at its own member entry (BotSnapshot::party_view_index, party_member_index).
 *
 * Off unless Bot.Snapshot.PartyView is on. A group with a member in combat, or whose leader is on a dungeon or raid map, is rebuilt every
 * tick; an idle group one tick in Bot.Snapshot.PartyViewIdleTicks (its last view is published again in between, with the same generation).
 * The loose-mob list and the attacker counts need Bot.Snapshot.PartyThreat too.
 */

#ifndef TRINITYCORE_MODULE_BOT_PARTY_VIEW_H
#define TRINITYCORE_MODULE_BOT_PARTY_VIEW_H

#include "Define.h"
#include "ModuleTier2.h"

#include <unordered_map>
#include <utility>
#include <vector>

class Player;

// What one group's view keeps between ticks: the view published last (reused on the ticks it is not rebuilt), when each loose mob was first
// seen on a non-tank (LooseMob::since_ms), and the tick the group was last seen (for pruning).
struct PartyViewWatch
{
    forever_module::PartyView View;
    std::vector<forever_module::PartyMember> Members;
    std::unordered_map<uint64, uint32> LooseSinceMs;   // unit reference -> the game time it went loose
    bool Instance = false;                             // the leader was on a dungeon or raid map at the last build
    uint64 LastSeenTick = 0;
};
using PartyViewWatchMap = std::unordered_map<uint64, PartyViewWatch>; // keyed by the group's guid counter

// Where a bot's group landed in the published sections: (view index, member index), both kNoPartyView when the group was not built.
using PartyViewSlotMap = std::unordered_map<uint64, std::pair<uint32, uint32>>; // keyed by the bot's guid counter

// Whether Bot.Snapshot.PartyView is on (read once).
bool PartyViewsEnabled();

// Builds the party views of the groups of `bots` (the module's online bots, world thread), one view per group however many of its members are
// bots, into `parties`, at most maxViews views and maxMembers member entries. `watch` is this module's per-group memory, pruned here of the
// groups no bot of the list is in. `slots` is filled with each bot's (view, member) indexes. `tick` is the snapshot tick (the idle stagger).
void BuildPartyViews(std::vector<Player*> const& bots, uint32 maxViews, uint32 maxMembers, uint64 tick, PartyViewWatchMap& watch,
                     ModuleSnapshotParties& parties, PartyViewSlotMap& slots);

#endif // TRINITYCORE_MODULE_BOT_PARTY_VIEW_H

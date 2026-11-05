/*
 * Spell rank groups for the spells that have no rank chain in the client data (docs 118-spell-rank-chains in forever-ops). A module names rank 1 of
 * a spell and the core casts the highest rank the bot knows (HighestKnownRank in ModuleBotActions.cpp); where SpellMgr's chain ends at rank 1, these
 * groups give the higher ranks. The grouping itself is BotSpellRankRules.h. Built once at startup, read-only after that.
 */

#ifndef TRINITY_BOT_SPELL_RANKS_H
#define TRINITY_BOT_SPELL_RANKS_H

#include "Define.h"

class Player;

namespace BotSpellRanks
{
// Builds the groups from the class trainers' spells and the SkillLineAbility spells that share a group with one of them. Spells with a native rank
// chain are left out. Call once, after SpellMgr::LoadSpellRanks and ObjectMgr::LoadTrainers (BotMgr::LoadRegistry). Logs the groups it leaves out.
void Build();

// The highest rank at or above spellId in its group that the bot knows and may use at its level; 0 when spellId is in no group or the bot knows
// no such rank.
uint32 HighestKnownInGroup(Player const& bot, uint32 spellId);

// The next rank of spellId in its group; 0 for the last rank or a spell in no group.
uint32 NextRank(uint32 spellId);
}

#endif // TRINITY_BOT_SPELL_RANKS_H

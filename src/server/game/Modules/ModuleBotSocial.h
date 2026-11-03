/*
 * Bot social actions (SDK ActionRequest fields 23 to 26, additive after protocol 20): duels, battleground queues, guild invitations and gift mail.
 * Each one runs the code a player's client reaches, on the bot's own session, and checks every precondition again when it runs (the module
 * decided some time ago):
 *
 *  - DuelAction: challenge (the real duel spell, so the zone and "already dueling" checks are a player's), accept, decline;
 *  - BattlegroundAction: queue solo (the real BattlemasterJoin handler), leave the queue, leave the battleground. A queued bot is not yet sent to the
 *    battleground by anything: accepting the invitation is not part of these actions;
 *  - GuildAction: accept or decline the pending invitation, leave the guild;
 *  - GiftMailAction: one whole stack of a small, tradable, not soulbound, not quest item and/or some copper, mailed to a character, postage paid.
 *
 * Settings: Bot.Social.Enabled (default 0), Bot.Social.GiftMaxCopper (default 10000: the most copper, and the most vendor value of the item, a
 * gift may carry). All four refuse a bot the requesting module does not control.
 */
#pragma once

#include "Define.h"

namespace ModuleWire
{
struct ActionRequestMsg;
struct ActionReplyMsg;
}

namespace ModuleBotSocial
{
void ExecuteDuel(ModuleWire::ActionRequestMsg const& request, ModuleWire::ActionReplyMsg& reply, uint32 module);
void ExecuteBattleground(ModuleWire::ActionRequestMsg const& request, ModuleWire::ActionReplyMsg& reply, uint32 module);
void ExecuteGuild(ModuleWire::ActionRequestMsg const& request, ModuleWire::ActionReplyMsg& reply, uint32 module);
void ExecuteGiftMail(ModuleWire::ActionRequestMsg const& request, ModuleWire::ActionReplyMsg& reply, uint32 module);
// Protocol 21: EmoteAction and LfgJoinAction.
void ExecuteEmote(ModuleWire::ActionRequestMsg const& request, ModuleWire::ActionReplyMsg& reply, uint32 module);
void ExecuteLfgJoin(ModuleWire::ActionRequestMsg const& request, ModuleWire::ActionReplyMsg& reply, uint32 module);
}

/*
 * Bots and the trade window (plan: .opencode/docs/plan-bot-trade.md). A bot has no client, so a trade with it is run on its behalf:
 *
 *  - when a player acts in a trade with a bot, the module that controls the bot is told (BotTradeEvent, hook PlayerScript.OnBotTrade);
 *  - the module answers with BotTradeAction (begin, set item, clear item, set gold, accept, cancel, list bags), which runs the real
 *    trade handlers on the bot's session, so the partner's window and the checks are the ones a real player's trade has;
 *  - BotSpeakAction makes a bot say a line or whisper a player.
 *
 * Settings: Bot.Trade.Enabled (default 0), Bot.Trade.Scope (0 anyone, 1 a member of the bot's group or a character on the bot's own
 * account; default 1), Bot.Trade.KeepConsumables (default 1: food, potions, reagents and ammo are not offered).
 */
#pragma once

#include "Define.h"

class Player;

namespace ModuleWire
{
struct ActionRequestMsg;
struct ActionReplyMsg;
}

namespace ModuleBotTrade
{
// BotTradeEvent.Status
enum Status : uint32
{
    Requested = 0,       // a player asked the bot to trade
    Opened = 1,          // the window opened
    Changed = 2,         // the partner changed its items or gold
    PartnerAccepted = 3, // the partner pressed Accept
    Cancelled = 4,
    Completed = 5
};

// `partner` did something in its trade. When its trade partner is a bot that a module controls, that module is told. Does nothing when
// bot trade is off, when the partner is a bot itself, or when the partner is not allowed to trade with the bot (a request from such a
// partner is cancelled).
void Notify(Player* partner, uint32 status);

// A trade between a and b completed: told for each of them that is a bot.
void NotifyCompleted(Player* a, Player* b);

// BotSpeakAction and BotTradeAction, run on the world thread. `module` is the requesting module; both refuse a bot it does not control.
void ExecuteSpeak(ModuleWire::ActionRequestMsg const& request, ModuleWire::ActionReplyMsg& reply, uint32 module);
void ExecuteTrade(ModuleWire::ActionRequestMsg const& request, ModuleWire::ActionReplyMsg& reply, uint32 module);
}

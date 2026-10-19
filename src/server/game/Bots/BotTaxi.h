/*
 * Flight points for bots. A bot knows every flight node of its faction and flies free: there is no client to visit a flight master and no purse worth
 * charging. A module sends ActionType::kTaxi (Modules/ModuleBotActions.cpp); Player::ActivateTaxiPathTo asks BotFliesFree whether to charge for it.
 *
 *   Bot.Taxi.Enabled      (default 1)  a module may make a bot fly
 *   Bot.Taxi.Free         (default 1)  a bot's flight costs nothing
 *   Bot.Taxi.MinGainYards (default 600) a flight must bring the bot at least this much closer to where it wants to go
 *
 * An alt (a player's own character playing as a bot) keeps the player's rules: it flies only where its character has been, and pays.
 */
#pragma once

#include "Define.h"

class Player;

namespace BotTaxi
{
bool Enabled();
float MinGainYards();

// Whether this player's flights are free: a bot made by BotMgr (not an alt), with Bot.Taxi.Free on.
bool FliesFree(Player const& player);
}

#include "BotTaxi.h"
#include "BotMgr.h"
#include "Config.h"
#include "DB2Stores.h"
#include "DB2Structure.h"
#include "Player.h"
#include "WorldSession.h"

namespace BotTaxi
{
bool Enabled()
{
    static bool const value = sConfigMgr->GetBoolDefault("Bot.Taxi.Enabled", true);
    return value;
}

float MinGainYards()
{
    static float const value = std::max(0.0f, sConfigMgr->GetFloatDefault("Bot.Taxi.MinGainYards", 600.0f));
    return value;
}

bool FliesFree(Player const& player)
{
    static bool const free = sConfigMgr->GetBoolDefault("Bot.Taxi.Free", true);
    WorldSession const* session = player.GetSession();
    return free && session && session->IsBot() && !sBotMgr->IsActiveAlt(player.GetGUID().GetCounter());
}

void LearnAllNodes(Player& bot)
{
    if (!Enabled())
        return;
    WorldSession const* session = bot.GetSession();
    if (!session || !session->IsBot() || sBotMgr->IsActiveAlt(bot.GetGUID().GetCounter()))
        return;
    TaxiNodeFlags const faction = bot.GetTeam() == HORDE ? TaxiNodeFlags::ShowOnHordeMap : TaxiNodeFlags::ShowOnAllianceMap;
    for (TaxiNodesEntry const* node : sTaxiNodesStore)
        if (node->IsPartOfTaxiNetwork() && node->GetFlags().HasFlag(faction))
            bot.m_taxi.SetTaximaskNode(node->ID);
}
}

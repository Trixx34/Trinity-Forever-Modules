#include "ModuleBotTrade.h"
#include "BotMgr.h"
#include "BotTalentSpec.h"
#include "Config.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "ModuleMgr.h"
#include "ModuleWire.h"
#include "ObjectAccessor.h"
#include "Opcodes.h"
#include "Player.h"
#include "TradeData.h"
#include "TradePackets.h"
#include "WorldPacket.h"
#include "WorldSession.h"

namespace ModuleBotTrade
{
namespace
{
constexpr char const* TradeHook = "PlayerScript.OnBotTrade";

// A chat line is short; this only guards against abuse (the same bound as a message to a player).
constexpr size_t MaxSpeakBytes = 255;

// The trade window holds six traded slots.
constexpr uint32 TradedSlots = TRADE_SLOT_TRADED_COUNT;

bool TradeEnabled()
{
    return sConfigMgr->GetBoolDefault("Bot.Trade.Enabled", false);
}

// Bot.Trade.Scope: 0 anyone. 1 (default) a member of the bot's group, or a character on the bot's own account (the owner of an alt).
bool PartnerAllowed(Player const* bot, Player const* partner)
{
    if (sConfigMgr->GetIntDefault("Bot.Trade.Scope", 1) == 0)
        return true;
    if (bot->GetSession()->GetAccountId() == partner->GetSession()->GetAccountId())
        return true;
    return bot->IsInSameGroupWith(partner);
}

bool IsControlledBot(Player const* player)
{
    return player && player->GetSession() && player->GetSession()->IsBot();
}

Player* FindControlledBot(uint64 botGuid, uint32 module, ModuleWire::ActionReplyMsg& reply)
{
    Player* bot = ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(botGuid));
    if (!IsControlledBot(bot))
    {
        reply.Reason = "the bot is not online";
        return nullptr;
    }
    if (!sModuleMgr->ControlsBot(botGuid, module))
    {
        reply.Reason = "the module does not control that bot";
        return nullptr;
    }
    return bot;
}

ModuleWire::BotTradeEvent BuildEvent(Player const* bot, Player const* partner, uint32 status)
{
    ModuleWire::BotTradeEvent event;
    event.BotGuid = bot->GetGUID().GetCounter();
    event.PartnerGuid = partner->GetGUID().GetCounter();
    event.Status = status;
    if (TradeData const* offered = partner->GetTradeData())
    {
        event.PartnerCopper = offered->GetMoney();
        for (uint32 slot = 0; slot < TradedSlots; ++slot)
            if (Item const* item = offered->GetItem(TradeSlots(slot)))
                event.Offered.push_back({ slot, item->GetEntry(), item->GetCount() });
    }
    return event;
}

void Send(Player* bot, Player* partner, uint32 status)
{
    sModuleMgr->DispatchBotTrade(TradeHook, BuildEvent(bot, partner, status));
}

// Food, potions, reagents and ammo are what a bot's own play uses; they are not offered unless Bot.Trade.KeepConsumables is 0.
bool KeptByBot(ItemTemplate const* itemTemplate)
{
    if (!sConfigMgr->GetBoolDefault("Bot.Trade.KeepConsumables", true))
        return false;
    switch (itemTemplate->GetClass())
    {
        case ITEM_CLASS_CONSUMABLE:
        case ITEM_CLASS_REAGENT:
        case ITEM_CLASS_PROJECTILE:
            return true;
        default:
            return false;
    }
}
} // namespace

void Notify(Player* partner, uint32 status)
{
    if (!partner || !TradeEnabled() || !partner->GetSession() || partner->GetSession()->IsBot())
        return;
    TradeData const* data = partner->GetTradeData();
    Player* bot = data ? data->GetTrader() : nullptr;
    if (!IsControlledBot(bot))
        return;

    if (status == Requested && !PartnerAllowed(bot, partner))
    {
        partner->TradeCancel(true);
        return;
    }
    Send(bot, partner, status);
}

void NotifyCompleted(Player* a, Player* b)
{
    if (!TradeEnabled() || !a || !b)
        return;
    if (IsControlledBot(b) && !IsControlledBot(a))
        Send(b, a, Completed);
    if (IsControlledBot(a) && !IsControlledBot(b))
        Send(a, b, Completed);
}

void ExecuteSpeak(ModuleWire::ActionRequestMsg const& request, ModuleWire::ActionReplyMsg& reply, uint32 module)
{
    if (request.Text.empty() || request.Text.size() > MaxSpeakBytes)
    {
        reply.Reason = "text must be 1 to 255 bytes";
        return;
    }
    Player* bot = FindControlledBot(request.BotGuid, module, reply);
    if (!bot)
        return;

    if (request.SpeakKind == 0)
    {
        bot->Say(request.Text, LANG_UNIVERSAL);
        reply.Success = true;
        return;
    }
    if (request.SpeakKind == 1)
    {
        Player* target = ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(request.PlayerGuid));
        if (!target || !target->GetSession())
        {
            reply.Reason = "the player is not online";
            return;
        }
        bot->Whisper(request.Text, LANG_UNIVERSAL, target);
        reply.Success = true;
        return;
    }
    reply.Reason = "unknown speak kind";
}

void ExecuteSpendTalents(ModuleWire::ActionRequestMsg const& request, ModuleWire::ActionReplyMsg& reply, uint32 module)
{
    if (request.TalentRole > BotTalentSpec::LastRole)
    {
        reply.Reason = "role must be 0 to 3";
        return;
    }
    if (request.TalentSpec > BotTalentSpec::LastSpec)
    {
        reply.Reason = "spec must be 0 to 28";
        return;
    }
    Player* bot = FindControlledBot(request.BotGuid, module, reply);
    if (!bot)
        return;
    if (request.TalentSpec && BotTalentSpec::Get(request.TalentSpec)->ClassId != bot->GetClass())
    {
        reply.Reason = "the spec does not belong to the bot's class";
        return;
    }
    if (char const* why = BotMgr::SetTalentRole(*bot, static_cast<uint8>(request.TalentRole), static_cast<uint8>(request.TalentSpec)))
    {
        reply.Reason = why;
        return;
    }
    reply.Success = true;
}

void ExecuteTrade(ModuleWire::ActionRequestMsg const& request, ModuleWire::ActionReplyMsg& reply, uint32 module)
{
    if (!TradeEnabled())
    {
        reply.Reason = "Bot.Trade.Enabled is off";
        return;
    }
    Player* bot = FindControlledBot(request.BotGuid, module, reply);
    if (!bot)
        return;
    WorldSession* session = bot->GetSession();

    // kListBags needs no open trade: the items the bot could offer now.
    if (request.TradeOp == 6)
    {
        bot->ForEachItem(ItemSearchLocation::Inventory, [&](Item* item)
        {
            ItemTemplate const* itemTemplate = item->GetTemplate();
            if (itemTemplate && !item->IsInTrade() && item->CanBeTraded(false, true) && !KeptByBot(itemTemplate))
            {
                ModuleWire::TradeBagItemMsg entry;
                entry.ItemGuid = item->GetGUID().GetCounter();
                entry.Entry = item->GetEntry();
                entry.Count = item->GetCount();
                entry.Name = itemTemplate->GetName(DEFAULT_LOCALE);
                reply.BagItems.push_back(std::move(entry));
            }
            return ItemSearchCallbackResult::Continue;
        });
        reply.Success = true;
        return;
    }

    TradeData* mine = bot->GetTradeData();
    if (!mine)
    {
        reply.Reason = "the bot has no trade open";
        return;
    }
    Player* partner = mine->GetTrader();
    if (!partner || !partner->GetSession() || partner->GetSession()->IsBot() || !PartnerAllowed(bot, partner))
    {
        bot->TradeCancel(true);
        reply.Reason = "the trade partner is not allowed to trade with this bot";
        return;
    }

    switch (request.TradeOp)
    {
        case 0: // kBegin
        {
            WorldPackets::Trade::BeginTrade packet{ WorldPacket(CMSG_BEGIN_TRADE) };
            session->HandleBeginTradeOpcode(packet);
            reply.Success = true;
            return;
        }
        case 1: // kSetItem
        {
            if (request.TradeSlot >= TradedSlots)
            {
                reply.Reason = "trade slot must be 0 to 5";
                return;
            }
            Item* item = bot->GetItemByGuid(ObjectGuid::Create<HighGuid::Item>(request.ItemGuid));
            if (!item || !item->CanBeTraded(false, true) || item->IsInTrade())
            {
                reply.Reason = "the item is not in the bot's bags or cannot be traded";
                return;
            }
            WorldPackets::Trade::SetTradeItem packet{ WorldPacket(CMSG_SET_TRADE_ITEM) };
            packet.TradeSlot = uint8(request.TradeSlot);
            packet.PackSlot = item->GetBagSlot();
            packet.ItemSlotInPack = item->GetSlot();
            session->HandleSetTradeItemOpcode(packet);
            TradeData const* after = bot->GetTradeData();
            reply.Success = after && after->GetItem(TradeSlots(request.TradeSlot)) == item;
            if (!reply.Success)
                reply.Reason = "the item could not be placed in the trade";
            return;
        }
        case 2: // kClearItem
        {
            if (request.TradeSlot >= TradedSlots)
            {
                reply.Reason = "trade slot must be 0 to 5";
                return;
            }
            WorldPackets::Trade::ClearTradeItem packet{ WorldPacket(CMSG_CLEAR_TRADE_ITEM) };
            packet.TradeSlot = uint8(request.TradeSlot);
            session->HandleClearTradeItemOpcode(packet);
            reply.Success = true;
            return;
        }
        case 3: // kSetGold
        {
            WorldPackets::Trade::SetTradeGold packet{ WorldPacket(CMSG_SET_TRADE_GOLD) };
            packet.Coinage = request.Copper;
            session->HandleSetTradeGoldOpcode(packet);
            TradeData const* after = bot->GetTradeData();
            reply.Success = after && after->GetMoney() == request.Copper;
            if (!reply.Success)
                reply.Reason = "the bot does not have that much gold";
            return;
        }
        case 4: // kAccept: the state index is the partner's, as a client sends it
        {
            TradeData const* theirs = partner->GetTradeData();
            if (!theirs)
            {
                reply.Reason = "the partner has no trade open";
                return;
            }
            WorldPackets::Trade::AcceptTrade packet{ WorldPacket(CMSG_ACCEPT_TRADE) };
            packet.StateIndex = theirs->GetServerStateIndex();
            session->HandleAcceptTradeOpcode(packet);
            reply.Success = true;
            return;
        }
        case 5: // kCancel
            bot->TradeCancel(true);
            reply.Success = true;
            return;
        default:
            break;
    }
    reply.Reason = "unknown trade operation";
}
} // namespace ModuleBotTrade

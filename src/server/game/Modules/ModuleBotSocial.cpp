#include "ModuleBotSocial.h"
#include "ModuleBotSocialRules.h"
#include "BattlegroundMgr.h"
#include "BattlegroundPackets.h"
#include "CharacterCache.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "DuelPackets.h"
#include "GuildMgr.h"
#include "Guild.h"
#include "GuildPackets.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Mail.h"
#include "ModuleMgr.h"
#include "ModuleWire.h"
#include "ObjectAccessor.h"
#include "Opcodes.h"
#include "Player.h"
#include "RBAC.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"

namespace ModuleBotSocial
{
namespace
{
constexpr float DuelRangeYards = 40.0f;

bool SocialEnabled()
{
    return sConfigMgr->GetBoolDefault("Bot.Social.Enabled", false);
}

uint64 GiftMaxCopper()
{
    int64 const value = sConfigMgr->GetInt64Default("Bot.Social.GiftMaxCopper", 10000);
    return value > 0 ? uint64(value) : 0;
}

Player* FindControlledBot(uint64 botGuid, uint32 module, ModuleWire::ActionReplyMsg& reply)
{
    Player* bot = ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(botGuid));
    if (!bot || !bot->GetSession() || !bot->GetSession()->IsBot())
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

// The bot for a social action: the setting is on and the module controls the bot.
Player* Prepare(ModuleWire::ActionRequestMsg const& request, ModuleWire::ActionReplyMsg& reply, uint32 module)
{
    if (!SocialEnabled())
    {
        reply.Reason = "Bot.Social.Enabled is off";
        return nullptr;
    }
    return FindControlledBot(request.BotGuid, module, reply);
}
} // namespace

void ExecuteDuel(ModuleWire::ActionRequestMsg const& request, ModuleWire::ActionReplyMsg& reply, uint32 module)
{
    if (request.SocialOp > 2)
    {
        reply.Reason = "duel op must be 0 to 2";
        return;
    }
    Player* bot = Prepare(request, reply, module);
    if (!bot)
        return;
    WorldSession* session = bot->GetSession();

    switch (request.SocialOp)
    {
        case 0: // kChallenge
        {
            Player* target = ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(request.PlayerGuid));
            if (!target || target == bot || !target->GetSession())
            {
                reply.Reason = "the other player is not online";
                return;
            }
            if (!bot->IsAlive() || !target->IsAlive())
            {
                reply.Reason = "a duelist is dead";
                return;
            }
            if (bot->duel || target->duel)
            {
                reply.Reason = "a duelist is already in a duel";
                return;
            }
            if (bot->GetMapId() != target->GetMapId() || !bot->IsWithinDist(target, DuelRangeYards))
            {
                reply.Reason = "the other player is too far away";
                return;
            }
            // the packet a client sends: the handler casts the duel spell, whose effect checks the zone, ignore list and flag placement
            WorldPackets::Duel::CanDuel packet{ WorldPacket(CMSG_CAN_DUEL) };
            packet.TargetGUID = target->GetGUID();
            session->HandleCanDuel(packet);
            reply.Success = bot->duel && bot->duel->Opponent == target;
            if (!reply.Success)
                reply.Reason = "the duel could not be started here";
            return;
        }
        case 1: // kAccept
        {
            if (!bot->duel || bot->duel->Initiator == bot || bot->duel->State != DUEL_STATE_CHALLENGED || !bot->duel->Opponent ||
                bot->duel->Opponent->GetGUID().GetCounter() != request.PlayerGuid)
            {
                reply.Reason = "that player has not challenged the bot to a duel";
                return;
            }
            session->HandleDuelAccepted(bot->GetDuelArbiter());
            reply.Success = bot->duel && bot->duel->State == DUEL_STATE_COUNTDOWN;
            if (!reply.Success)
                reply.Reason = "the duel could not be accepted";
            return;
        }
        default: // kDecline
        {
            if (!bot->duel || !bot->duel->Opponent || bot->duel->Opponent->GetGUID().GetCounter() != request.PlayerGuid)
            {
                reply.Reason = "the bot has no duel with that player";
                return;
            }
            session->HandleDuelCancelled();
            reply.Success = true;
            return;
        }
    }
}

void ExecuteBattleground(ModuleWire::ActionRequestMsg const& request, ModuleWire::ActionReplyMsg& reply, uint32 module)
{
    if (request.SocialOp > 2)
    {
        reply.Reason = "battleground op must be 0 to 2";
        return;
    }
    Player* bot = Prepare(request, reply, module);
    if (!bot)
        return;

    if (request.SocialOp == 2) // kLeave
    {
        if (!bot->InBattleground())
        {
            reply.Reason = "the bot is not in a battleground";
            return;
        }
        WorldPackets::Battleground::BattlefieldLeave packet{ WorldPacket(CMSG_BATTLEFIELD_LEAVE) };
        bot->GetSession()->HandleBattlefieldLeaveOpcode(packet);
        reply.Success = !bot->InBattleground();
        if (!reply.Success)
            reply.Reason = "the bot cannot leave now (in combat)";
        return;
    }

    if (request.BattlegroundId == 0 || request.BattlegroundId > 0xFFFF)
    {
        reply.Reason = "battleground id is not valid";
        return;
    }
    BattlegroundQueueTypeId const queueId = BattlegroundMgr::BGQueueTypeId(uint16(request.BattlegroundId), BattlegroundQueueIdType::Battleground, false, 0);
    if (!BattlegroundMgr::IsValidQueueId(queueId))
    {
        reply.Reason = "battleground id is not valid";
        return;
    }

    if (request.SocialOp == 1) // kLeaveQueue
    {
        if (bot->GetBattlegroundQueueIndex(queueId) >= PLAYER_MAX_BATTLEGROUND_QUEUES)
        {
            reply.Reason = "the bot is not in that queue";
            return;
        }
        bot->RemoveBattlegroundQueueId(queueId);
        sBattlegroundMgr->GetBattlegroundQueue(queueId).RemovePlayer(bot->GetGUID(), true);
        reply.Success = true;
        return;
    }

    // kQueue: the preconditions a player meets at the battlemaster, checked again here
    if (bot->InBattleground())
    {
        reply.Reason = "the bot is already in a battleground";
        return;
    }
    if (!bot->IsAlive())
    {
        reply.Reason = "the bot is dead";
        return;
    }
    if (bot->GetGroup())
    {
        reply.Reason = "the bot is in a group; only a solo queue is supported";
        return;
    }
    if (bot->GetBattlegroundQueueIndex(queueId) < PLAYER_MAX_BATTLEGROUND_QUEUES)
    {
        reply.Reason = "the bot is already in that queue";
        return;
    }
    // the real handler: level bracket, faction, deserter, random queue and free slot checks are the ones a player has
    WorldPackets::Battleground::BattlemasterJoin packet{ WorldPacket(CMSG_BATTLEMASTER_JOIN) };
    packet.QueueIDs.push_back(queueId.GetPacked());
    bot->GetSession()->HandleBattlemasterJoinOpcode(packet);
    reply.Success = bot->GetBattlegroundQueueIndex(queueId) < PLAYER_MAX_BATTLEGROUND_QUEUES;
    if (!reply.Success)
        reply.Reason = "the bot could not join that queue (level, deserter or the battleground is off)";
}

void ExecuteGuild(ModuleWire::ActionRequestMsg const& request, ModuleWire::ActionReplyMsg& reply, uint32 module)
{
    if (request.SocialOp > 2)
    {
        reply.Reason = "guild op must be 0 to 2";
        return;
    }
    Player* bot = Prepare(request, reply, module);
    if (!bot)
        return;
    WorldSession* session = bot->GetSession();

    if (request.SocialOp == 2) // kLeave
    {
        Guild* guild = bot->GetGuild();
        if (!guild)
        {
            reply.Reason = "the bot is not in a guild";
            return;
        }
        if (guild->GetLeaderGUID() == bot->GetGUID())
        {
            reply.Reason = "the bot is the guild master";
            return;
        }
        WorldPackets::Guild::GuildLeave packet{ WorldPacket(CMSG_GUILD_LEAVE) };
        session->HandleGuildLeave(packet);
        reply.Success = !bot->GetGuildId();
        if (!reply.Success)
            reply.Reason = "the bot could not leave the guild";
        return;
    }

    // accept and decline need an invitation that is still pending, and a bot that is in no guild
    if (bot->GetGuildId() || !bot->GetGuildIdInvited() || !sGuildMgr->GetGuildById(bot->GetGuildIdInvited()))
    {
        reply.Reason = "no pending guild invitation";
        return;
    }
    if (request.SocialOp == 0)
    {
        WorldPackets::Guild::AcceptGuildInvite packet{ WorldPacket(CMSG_ACCEPT_GUILD_INVITE) };
        session->HandleGuildAcceptInvite(packet);
        reply.Success = bot->GetGuildId() != 0;
        if (!reply.Success)
            reply.Reason = "the bot could not join the guild";
    }
    else
    {
        WorldPackets::Guild::GuildDeclineInvitation packet{ WorldPacket(CMSG_GUILD_DECLINE_INVITATION) };
        session->HandleGuildDeclineInvitation(packet);
        reply.Success = true;
    }
}

void ExecuteGiftMail(ModuleWire::ActionRequestMsg const& request, ModuleWire::ActionReplyMsg& reply, uint32 module)
{
    if (!GiftSubjectValid(request.Text))
    {
        reply.Reason = "subject must be 1 to 64 bytes";
        return;
    }
    if (request.ItemGuid == 0 && request.Copper == 0)
    {
        reply.Reason = "nothing to send";
        return;
    }
    Player* bot = Prepare(request, reply, module);
    if (!bot)
        return;

    ObjectGuid const receiverGuid = ObjectGuid::Create<HighGuid::Player>(request.PlayerGuid);
    if (receiverGuid == bot->GetGUID())
    {
        reply.Reason = "a bot does not mail itself";
        return;
    }
    CharacterCacheEntry const* receiverInfo = sCharacterCache->GetCharacterCacheByGuid(receiverGuid);
    if (!receiverInfo)
    {
        reply.Reason = "the recipient does not exist";
        return;
    }
    Player* receiverOnline = ObjectAccessor::FindConnectedPlayer(receiverGuid);
    if (receiverOnline && receiverOnline->GetMailSize() > 100)
    {
        reply.Reason = "the recipient's mailbox is full";
        return;
    }
    if (bot->GetTeam() != Player::TeamForRace(receiverInfo->Race) && !bot->GetSession()->HasPermission(rbac::RBAC_PERM_TWO_SIDE_INTERACTION_MAIL))
    {
        reply.Reason = "the recipient is of the other faction";
        return;
    }

    // the gift, checked against the bag as it is now
    Item* item = nullptr;
    uint64 itemValue = 0;
    if (request.ItemGuid)
    {
        item = bot->GetItemByGuid(ObjectGuid::Create<HighGuid::Item>(request.ItemGuid));
        ItemTemplate const* itemTemplate = item ? item->GetTemplate() : nullptr;
        if (!item || !itemTemplate || item->IsEquipped() || item->IsInTrade())
        {
            reply.Reason = "the item is not in the bot's bags";
            return;
        }
        if (request.Count != item->GetCount())
        {
            reply.Reason = "only a whole stack is sent; the count must be the stack's";
            return;
        }
        if (item->IsSoulBound() || item->IsBag() || !item->CanBeTraded(true, false))
        {
            reply.Reason = "the item is bound or cannot be mailed";
            return;
        }
        if (itemTemplate->GetClass() == ITEM_CLASS_QUEST || itemTemplate->GetStartQuest() || itemTemplate->HasFlag(ITEM_FLAG_IS_BOUND_TO_ACCOUNT))
        {
            reply.Reason = "quest and account bound items are not given away";
            return;
        }
        itemValue = uint64(itemTemplate->GetSellPrice()) * item->GetCount();
    }
    if (!GiftWithinCap(request.Copper, itemValue, GiftMaxCopper()))
    {
        reply.Reason = "the gift is worth more than Bot.Social.GiftMaxCopper";
        return;
    }
    uint64 const postage = 30;
    if (!bot->HasEnoughMoney(request.Copper + postage))
    {
        reply.Reason = "the bot does not have enough gold for the gift and the postage";
        return;
    }

    // as HandleSendMail does it
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    MailDraft draft(request.Text, "");
    if (item)
    {
        item->SetNotRefundable(bot);
        bot->MoveItemFromInventory(item->GetBagSlot(), item->GetSlot(), true);
        item->DeleteFromInventoryDB(trans);
        item->SetOwnerGUID(receiverGuid);
        item->SetState(ITEM_CHANGED);
        item->SaveToDB(trans);
        draft.AddItem(item);
    }
    bot->ModifyMoney(-int64(request.Copper + postage));
    uint32 const delay = sWorld->getIntConfig(CONFIG_MAIL_DELIVERY_DELAY);
    draft.AddMoney(request.Copper)
        .SendMailTo(trans, MailReceiver(receiverOnline, receiverGuid.GetCounter()), MailSender(bot), MAIL_CHECK_MASK_COPIED, delay);
    bot->SaveInventoryAndGoldToDB(trans);
    CharacterDatabase.CommitTransaction(trans);

    reply.Success = true;
    reply.CopperSpent = request.Copper + postage;
}
} // namespace ModuleBotSocial

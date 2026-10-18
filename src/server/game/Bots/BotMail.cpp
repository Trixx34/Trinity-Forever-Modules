/*
 * Mail for bots. A bot has no client, so nothing ever opens a mailbox for it: the money of an auction it sold, the item it bought or an
 * expired listing would stay in its mail for ever. This takes what is in the bot's delivered mail, the way HandleMailTakeMoney and
 * HandleMailTakeItem do for a player but without a mailbox, and deletes the mail that is empty afterwards. BotMgr calls it for each
 * online bot every Bot.Mail.CollectEverySec seconds and when a bot logs in (mail that arrived while it was away).
 *
 * Cash on delivery mail is left alone (a bot does not pay), and so is mail that is not delivered yet: the sale of an auction reaches its
 * owner after the mail delivery delay (Mail.DeliveryDelay).
 */

#include "WorldSession.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "Item.h"
#include "Log.h"
#include "Mail.h"
#include "Player.h"

uint32 WorldSession::CollectMailAsBot()
{
    Player* player = _player;
    if (!player || !_isBot)
        return 0;

    time_t const now = GameTime::GetGameTime();
    uint32 touched = 0;
    uint64 money = 0;
    uint32 items = 0;
    for (Mail* m : player->GetMails())
    {
        if (m->state == MAIL_STATE_DELETED || m->deliver_time > now || m->COD > 0)
            continue;

        bool changed = false;
        if (m->money > 0)
        {
            uint64 const amount = m->money;
            if (player->ModifyMoney(int64(amount), false))
            {
                money += amount;
                m->money = 0;
                changed = true;
            }
        }

        std::vector<MailItemInfo> const attached = m->items; // the list changes as items are taken
        for (MailItemInfo const& info : attached)
        {
            Item* item = player->GetMItem(info.item_guid);
            if (!item)
                continue;
            ItemPosCountVec destination;
            if (player->CanStoreItem(NULL_BAG, NULL_SLOT, destination, item, false) != EQUIP_ERR_OK)
                continue; // the bags are full: tried again at the next pass
            m->RemoveItem(info.item_guid);
            m->removedItems.push_back(info.item_guid);
            player->RemoveMItem(info.item_guid);
            item->SetState(ITEM_UNCHANGED);
            player->MoveItemToInventory(destination, item, true);
            ++items;
            changed = true;
        }

        if (m->money == 0 && m->items.empty())
        {
            m->state = MAIL_STATE_DELETED; // nothing left in it, and a bot never reads
            changed = true;
        }
        else if (changed)
            m->state = MAIL_STATE_CHANGED;
        if (changed)
            ++touched;
    }

    if (touched)
    {
        player->m_mailsUpdated = true;
        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        player->SaveInventoryAndGoldToDB(trans);
        player->_SaveMail(trans);
        CharacterDatabase.CommitTransaction(trans);
        if (money || items)
            TC_LOG_DEBUG("server.worldserver", "BotMgr: bot {} took {} copper and {} item(s) from its mail.", player->GetName(), money, items);
    }
    return touched;
}

/*
 * Module -> core actions. See ModuleActions.h.
 */

#include "ModuleActions.h"
#include "ModuleBotTrade.h"
#include "AuctionHouse/AuctionHouseMgr.h"
#include "Chat.h"
#include "Creature.h"
#include "Group.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "ModuleStore.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "AuctionHouseMgr.h"
#include "Server/Packets/AuctionHousePackets.h"
#include "Server/Packets/ItemPackets.h"
#include "ModuleMgr.h"
#include "MapManager.h"
#include "CharacterCache.h"
#include "CreatureData.h"
#include "Player.h"
#include "WorldSession.h"

#include <algorithm>

namespace
{

// Longest text a module may send a player. Chat lines are short; this only guards against abuse.
constexpr size_t MaxMessageBytes = 255;

// Quest group loot: the range a party member may be from the looter when the module sets none, and the most it may set.
constexpr uint32 DefaultShareRangeYards = 100;
constexpr uint32 MaxShareRangeYards = 200;

// Gives a copy of a quest item the looter just looted to each party member who still needs it. The looter keeps their own. A member
// is skipped when on another map, out of range, with no quest that needs the item, or with no room in the bags; none of that is an
// error. The module names the item, the core finds who gets it and gives it, so a module cannot give out items to anyone it likes.
void ShareQuestItem(Player* looter, ModuleWire::ActionRequestMsg const& request, ModuleWire::ActionReplyMsg& reply)
{
    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(request.ItemEntry);
    if (!proto || proto->GetClass() != ITEM_CLASS_QUEST)
    {
        reply.Reason = "item is not a quest item";
        return;
    }
    if (request.Count == 0 || request.Count > proto->GetMaxStackSize())
    {
        reply.Reason = "count must be from 1 to the item's stack size";
        return;
    }

    Group* group = looter->GetGroup();
    if (!group)
    {
        reply.Reason = "player is not in a group";
        return;
    }

    uint32 const range = request.RangeYards ? std::min(request.RangeYards, MaxShareRangeYards) : DefaultShareRangeYards;
    uint32 given = 0;
    for (GroupReference const& ref : group->GetMembers())
    {
        Player* member = ref.GetSource();
        if (!member || member == looter || !member->IsInWorld() || member->GetMap() != looter->GetMap())
            continue;
        if (!looter->IsWithinDist(member, float(range)) || !member->HasQuestForItem(request.ItemEntry))
            continue;

        ItemPosCountVec dest;
        if (member->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, request.ItemEntry, request.Count) != EQUIP_ERR_OK)
            continue;
        Item* item = member->StoreNewItem(dest, request.ItemEntry, true);
        if (!item)
            continue;

        member->SendNewItem(item, request.Count, true, false);
        ++given;
    }

    reply.Recipients = given;
    reply.Success = true;
    TC_LOG_DEBUG("modules.actions", "shared quest item {} x{} from player {} with {} party member(s)",
                 request.ItemEntry, request.Count, looter->GetGUID().ToString(), given);
}

void SellItem(Player* player, ModuleWire::ActionRequestMsg const& request, ModuleWire::ActionReplyMsg& reply)
{
    Item* item = player->GetItemByGuid(ObjectGuid::Create<HighGuid::Item>(request.ItemGuid));
    if (!item)
    {
        reply.Reason = "item is no longer in the player's bags";
        return;
    }

    if (item->GetEntry() != request.ItemEntry)
    {
        reply.Reason = "item entry does not match the request";
        return;
    }

    if (request.Count == 0 || player->GetItemCount(request.ItemEntry) < request.Count)
    {
        reply.Reason = "not enough of the item to sell";
        return;
    }

    ItemTemplate const* proto = item->GetTemplate();
    uint32 const price = proto ? proto->GetSellPrice() : 0;
    if (price == 0)
    {
        reply.Reason = "item has no vendor value";
        return;
    }

    // Refuse before removing anything: once the items are gone the money has to be credited.
    uint64 const total = uint64(price) * request.Count;
    if (player->GetMoney() > MAX_MONEY_AMOUNT - total)
    {
        reply.Reason = "gold cap would be exceeded";
        return;
    }

    // Count what was really removed, rather than trusting the return value of DestroyItemCount.
    uint32 const before = player->GetItemCount(request.ItemEntry);
    player->DestroyItemCount(request.ItemEntry, request.Count, true);
    uint32 const removed = before - player->GetItemCount(request.ItemEntry);

    uint64 const credit = uint64(price) * removed;
    if (credit && !player->ModifyMoney(static_cast<int64>(credit)))
    {
        TC_LOG_ERROR("modules", "sold {} x item {} for player {} but could not credit {} copper",
                     removed, request.ItemEntry, player->GetGUID().ToString(), credit);
        reply.Reason = "items were removed but the money could not be credited";
        return;
    }

    reply.CopperGained = credit;
    if (removed != request.Count)
    {
        reply.Reason = "only part of the requested count was removed";
        return;
    }

    reply.Success = true;
    TC_LOG_DEBUG("modules.actions", "sold {} x item {} [{}] for {} copper, player {}",
                 removed, request.ItemEntry, request.ItemGuid, credit, player->GetGUID().ToString());
}

// Buys `count` of `item_entry` from the vendor named by `vendor_guid`. Checks range, sell-list
// membership, price, and player money before removing anything.
void BuyItem(Player* player, ModuleWire::ActionRequestMsg const& request, ModuleWire::ActionReplyMsg& reply)
{
    // Find the vendor NPC and verify interaction range.
    ObjectGuid vendorGuid;
    vendorGuid.SetRawValue(0, request.VendorGuid);
    Creature* vendor = ObjectAccessor::GetCreature(*player, vendorGuid);
    if (!vendor || !vendor->IsInWorld())
    {
        reply.Reason = "vendor is not in the world";
        return;
    }
    if (!player->IsWithinDist(vendor, INTERACTION_DISTANCE))
    {
        reply.Reason = "vendor is out of interaction range";
        return;
    }
    if (!player->GetNPCIfCanInteractWith(vendor->GetGUID(), UNIT_NPC_FLAG_VENDOR, UNIT_NPC_FLAG_2_NONE))
    {
        reply.Reason = "cannot interact with that vendor";
        return;
    }

    // Look up the item on the vendor's sell list.
    VendorItemData const* vItems = vendor->GetVendorItems();
    if (!vItems || vItems->Empty())
    {
        reply.Reason = "vendor has nothing for sale";
        return;
    }
    VendorItem const* vItem = nullptr;
    for (uint32 slot = 0; slot < vItems->GetItemCount(); ++slot)
    {
        VendorItem const* cand = vItems->GetItem(slot);
        if (cand && cand->item == request.ItemEntry)
        {
            vItem = cand;
            break;
        }
    }
    if (!vItem)
    {
        reply.Reason = "item is not on this vendor's sell list";
        return;
    }

    // Price: buy price from the item template, extended cost overrides it.
    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(request.ItemEntry);
    if (!proto)
    {
        reply.Reason = "item template not found";
        return;
    }
    uint32 price = proto->GetBuyPrice();
    if (vItem->ExtendedCost)
    {
        ItemExtendedCostEntry const* iece = sItemExtendedCostStore.LookupEntry(vItem->ExtendedCost);
        if (iece && iece->Money)
            price = iece->Money;
    }
    if (price == 0)
    {
        reply.Reason = "item has no buy price";
        return;
    }

    // Reputation discount, same as the player-facing vendor path.
    price = uint32(std::floor(price * player->GetReputationPriceDiscount(vendor)));

    uint64 const total = uint64(price) * request.Count;
    if (total > MAX_MONEY_AMOUNT || player->GetMoney() < total)
    {
        reply.Reason = "not enough money";
        return;
    }

    // Bag space check before taking money.
    ItemPosCountVec dest;
    if (player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, request.ItemEntry, request.Count) != EQUIP_ERR_OK)
    {
        reply.Reason = "not enough bag space";
        return;
    }

    // Deduct money, add item.
    if (!player->ModifyMoney(static_cast<int64>(-total)))
    {
        reply.Reason = "could not deduct money";
        return;
    }
    Item* item = player->StoreNewItem(dest, request.ItemEntry, true);
    if (!item)
    {
        // Refund on failure.
        player->ModifyMoney(static_cast<int64>(total));
        reply.Reason = "could not add item to bags";
        return;
    }
    player->SendNewItem(item, request.Count, true, false);

    reply.CopperSpent = total;
    reply.Success = true;
    TC_LOG_DEBUG("modules.actions", "bought {} x item {} from vendor {} for {} copper, player {}",
                 request.Count, request.ItemEntry, request.VendorGuid, total, player->GetGUID().ToString());
}

// Auction House actions: browse, bid, buyout, list, cancel.
void AuctionHouse(Player* player, ModuleWire::ActionRequestMsg const& request, ModuleWire::ActionReplyMsg& reply)
{
    Creature* auctioneer = ObjectAccessor::GetCreature(*player, ObjectGuid::Create<HighGuid::Creature>(request.AuctioneerGuid));
    if (!auctioneer || !auctioneer->IsInWorld())
    {
        reply.Reason = "auctioneer is not in the world";
        return;
    }
    if (!player->IsWithinDist(auctioneer, INTERACTION_DISTANCE))
    {
        reply.Reason = "auctioneer is out of interaction range";
        return;
    }

    AuctionHouseObject* auctionHouse = sAuctionMgr->GetAuctionsMap(auctioneer->GetFaction());
    if (!auctionHouse)
    {
        reply.Reason = "no auction house for this faction";
        return;
    }

    switch (static_cast<uint32>(request.AhOp))
    {
        case 0: // kBrowse
        {
            // Acknowledge; a real module would page through auctions via separate queries.
            reply.Success = true;
            return;
        }

        case 1: // kBid
        {
            if (!request.AuctionGuid || !request.Bid)
            {
                reply.Reason = "auction_guid and bid are required";
                return;
            }
            AuctionPosting* auction = auctionHouse->GetAuction(request.AuctionGuid);
            if (!auction)
            {
                reply.Reason = "auction not found";
                return;
            }
            if (auction->Owner == player->GetGUID())
            {
                reply.Reason = "cannot bid on your own auction";
                return;
            }
            uint64 minBid = auction->BidAmount ? auction->BidAmount + auction->CalculateMinIncrement() : auction->MinBid;
            if (request.Bid < minBid)
            {
                reply.Reason = "bid too low";
                return;
            }
            if (auction->BuyoutOrUnitPrice && request.Bid >= auction->BuyoutOrUnitPrice)
            {
                // Treat a bid at or above buyout as a buyout.
                uint64 price = auction->BuyoutOrUnitPrice;
                if (!auction->IsCommodity())
                    price *= auction->GetTotalItemCount();
                if (player->GetMoney() < price)
                {
                    reply.Reason = "not enough money";
                    return;
                }
                CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
                auctionHouse->RemoveAuction(trans, auction);
                auctionHouse->SendAuctionSold(auction, nullptr, trans);
                auctionHouse->SendAuctionWon(auction, player, trans);
                player->ModifyMoney(static_cast<int64>(-price));
                CharacterDatabase.CommitTransaction(trans);
                reply.CopperSpent = price;
                reply.Success = true;
                return;
            }
            uint64 priceToPay = request.Bid - (auction->Bidder == player->GetGUID() ? auction->BidAmount : 0);
            if (player->GetMoney() < priceToPay)
            {
                reply.Reason = "not enough money";
                return;
            }
            CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
            player->ModifyMoney(static_cast<int64>(-priceToPay));
            auction->Bidder = player->GetGUID();
            auction->BidAmount = request.Bid;
            CharacterDatabase.CommitTransaction(trans);
            reply.CopperSpent = priceToPay;
            reply.Success = true;
            return;
        }

        case 2: // kBuyout
        {
            if (!request.AuctionGuid || !request.Buyout)
            {
                reply.Reason = "auction_guid and buyout are required";
                return;
            }
            AuctionPosting* auction = auctionHouse->GetAuction(request.AuctionGuid);
            if (!auction)
            {
                reply.Reason = "auction not found";
                return;
            }
            if (auction->Owner == player->GetGUID())
            {
                reply.Reason = "cannot buy out your own auction";
                return;
            }
            uint64 price = auction->BuyoutOrUnitPrice;
            if (!auction->IsCommodity())
                price *= auction->GetTotalItemCount();
            if (request.Buyout < price)
            {
                reply.Reason = "buyout price too low";
                return;
            }
            if (player->GetMoney() < price)
            {
                reply.Reason = "not enough money";
                return;
            }
            CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
            auctionHouse->RemoveAuction(trans, auction);
            auctionHouse->SendAuctionSold(auction, nullptr, trans);
            auctionHouse->SendAuctionWon(auction, player, trans);
            player->ModifyMoney(static_cast<int64>(-price));
            CharacterDatabase.CommitTransaction(trans);
            reply.CopperSpent = price;
            reply.Success = true;
            return;
        }

        case 3: // kList
        {
            // Listing requires splitting the item stack, computing the deposit, and creating a
            // full AuctionPosting with DB transaction. That is intentionally deferred; the
            // module can use kBuyout/kBid/kCancel on existing auctions instead.
            reply.Reason = "kList is not yet implemented for module actions";
            return;
        }

        case 4: // kCancel
        {
            if (!request.AuctionGuid)
            {
                reply.Reason = "auction_guid is required";
                return;
            }
            AuctionPosting* auction = auctionHouse->GetAuction(request.AuctionGuid);
            if (!auction)
            {
                reply.Reason = "auction not found";
                return;
            }
            if (auction->Owner != player->GetGUID())
            {
                reply.Reason = "not your auction";
                return;
            }
            uint64 cancelCost = 0;
            if (auction->BidAmount)
            {
                cancelCost = CalculatePct(auction->BidAmount, 5u);
                if (player->GetMoney() < cancelCost)
                {
                    reply.Reason = "not enough money for cancel fee";
                    return;
                }
            }
            CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
            if (cancelCost)
                player->ModifyMoney(static_cast<int64>(-cancelCost));
            auctionHouse->RemoveAuction(trans, auction);
            if (cancelCost)
                auctionHouse->SendAuctionCancelledToBidder(auction, trans);
            CharacterDatabase.CommitTransaction(trans);
            reply.CopperSpent = cancelCost;
            reply.Success = true;
            return;
        }

        default:
            reply.Reason = "unknown auction house operation";
            return;
    }
}

} // namespace

namespace
{

// Module store actions (doc 15). They need the module, not a player.
void ExecuteStoreAction(std::string const& moduleName, ModuleWire::ActionRequestMsg const& request, ModuleWire::ActionReplyMsg& reply)
{
    std::string error;
    switch (request.Kind)
    {
        case ModuleWire::ActionKind::StoreGet:
        {
            bool found = false;
            std::string value;
            error = ModuleStore::Get(moduleName, request.Key, found, value);
            reply.Found = found;
            reply.Value = std::move(value);
            break;
        }
        case ModuleWire::ActionKind::StoreSet:
            error = ModuleStore::Set(moduleName, request.Key, request.Value);
            break;
        case ModuleWire::ActionKind::StoreDelete:
        {
            bool existed = false;
            error = ModuleStore::Delete(moduleName, request.Key, existed);
            reply.Found = existed;
            break;
        }
        case ModuleWire::ActionKind::StoreList:
            error = ModuleStore::List(moduleName, request.Key, reply.Keys);
            break;
        default:
            error = "not a store action";
            break;
    }

    if (!error.empty())
    {
        reply.Reason = error;
        return;
    }
    reply.Success = true;
}

} // namespace

namespace
{

static uint32 AuctionFactionTemplate(ObjectGuid auctioneer)
{
    if (CreatureTemplate const* ct = sObjectMgr->GetCreatureTemplate(auctioneer.GetEntry()))
        return ct->faction;
    return 0;
}

static Creature* ResolveAuctioneerCreature(uint64 ref)
{
    if (forever_module::UnitRefType(ref) != uint8(HighGuid::Creature))
        return nullptr;
    uint32 const entry = forever_module::UnitRefEntry(ref);
    uint32 const counter = static_cast<uint32>(forever_module::UnitRefCounter(ref));
    Creature* found = nullptr;
    sMapMgr->DoForAllMaps([&](Map* map)
    {
        if (found)
            return;
        ObjectGuid const guid = ObjectGuid::Create<HighGuid::Creature>(0, map->GetId(), entry, counter);
        Creature* creature = map->GetCreature(guid);
        if (creature)
            found = creature;
    });
    return found;
}

void BuyItemAction(Player* primaryPlayer, ModuleWire::ActionRequestMsg const& request, ModuleWire::ActionReplyMsg& reply)
{
    Player* player = primaryPlayer;
    if (!player && request.PlayerGuid)
        player = ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(request.PlayerGuid));
    if (!player)
    {
        // BuyItem has no player in the schema; try to find one owning nothing specific: none.
        reply.Reason = "BuyItem requires an acting player, but the action schema does not identify one; add a player/buyer guid to BuyItemAction";
        return;
    }

    Creature* vendor = ResolveAuctioneerCreature(request.AuctioneerGuid);
    if (!vendor)
    {
        // Allow the module to pass an ObjectGuid-style raw vendor reference by interacting as with an NPC in range.
        // Fall back to nearest vendor? Refuse rather than guessing.
        reply.Reason = "vendor not found on any map";
        return;
    }

    Creature* creature = player->GetNPCIfCanInteractWith(vendor->GetGUID(), UNIT_NPC_FLAG_VENDOR, UNIT_NPC_FLAG_2_NONE);
    if (!creature)
    {
        reply.Reason = "vendor not found or cannot interact with it";
        return;
    }

    VendorItemData const* vendorItems = creature->GetVendorItems();
    if (!vendorItems)
    {
        reply.Reason = "vendor has no items to sell";
        return;
    }
    int32 slot = -1;
    for (uint32 i = 0; i < vendorItems->GetItemCount(); ++i)
    {
        if (VendorItem const* vi = vendorItems->GetItem(i))
            if (vi->Type == ITEM_VENDOR_TYPE_ITEM && vi->item == request.ItemEntry)
            {
                slot = static_cast<int32>(i);
                break;
            }
    }
    if (slot < 0)
    {
        reply.Reason = "item is not on the vendor's sell list";
        return;
    }

    // check money roughly
    uint64 price = 0;
    if (VendorItem const* vi = vendorItems->GetItem(static_cast<uint32>(slot)))
    {
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(vi->item);
        uint32 const buyPrice = proto ? proto->GetBuyPrice() : 0;
        price = uint64(buyPrice) * (request.Count ? request.Count : 1u);
    }
    if (price && !player->HasEnoughMoney(price))
    {
        reply.Reason = "not enough money";
        return;
    }

    auto const quantity = request.Count ? request.Count : 1u;
    WorldPackets::Item::BuyItem packet{ WorldPacket(CMSG_BUY_ITEM) };
    packet.Muid = slot + 1; // handler decrements
    packet.ItemType = ITEM_VENDOR_TYPE_ITEM;
    packet.VendorGUID = creature->GetGUID();
    packet.ContainerGUID = player->GetGUID(); // default backpack
    packet.Quantity = quantity;
    packet.Item.ItemID = request.ItemEntry;
    packet.Slot = -1;

    uint64 before = player->GetMoney();
    player->GetSession()->HandleBuyItemOpcode(packet);
    uint64 after = player->GetMoney();
    reply.CopperSpent = before > after ? before - after : price;
    reply.ItemEntry = request.ItemEntry;
    reply.Success = true;
}

void AuctionHouseAction(Player* primaryPlayer, ModuleWire::ActionRequestMsg const& request, ModuleWire::ActionReplyMsg& reply)
{
    switch (request.AuctionOp)
    {
        case 0: // kBrowse
        {
            Creature* auctioneer = ResolveAuctioneerCreature(request.AuctioneerGuid);
            if (!auctioneer)
            {
                reply.Reason = "auctioneer not found on any map";
                return;
            }
            AuctionHouseObject* ah = sAuctionMgr->GetAuctionsMap(auctioneer->GetCreatureTemplate()->faction);

            Player* anyPlayer = nullptr;
            for (auto const& [guid, player] : ObjectAccessor::GetPlayers())
            {
                anyPlayer = player;
                break;
            }
            if (!anyPlayer)
            {
                reply.Reason = "no online player to establish an auction house locale";
                return;
            }

            WorldPackets::AuctionHouse::AuctionListItemsResult result;
            ah->BuildListAuctionItems(result, anyPlayer, request.ItemEntry, 0, std::span<WorldPackets::AuctionHouse::AuctionSortDef const>{});

            uint32 total = result.TotalCount;
            for (WorldPackets::AuctionHouse::AuctionItem const& ai : result.Items)
            {
                ModuleWire::AuctionListingEvent event;
                event.AuctionGuid = static_cast<uint64>(ai.AuctionID);
                event.ItemEntry = (ai.Item && ai.Item->ItemID) ? static_cast<uint32>(ai.Item->ItemID) : request.ItemEntry;
                if (!event.ItemEntry)
                    event.ItemEntry = request.ItemEntry;
                event.Count = static_cast<uint32>(ai.Count);
                event.Bid = ai.BidAmount.value_or(0);
                if (ai.BuyoutPrice.has_value())
                    event.Buyout = ai.BuyoutPrice.value();
                else if (ai.UnitPrice.has_value())
                    event.Buyout = ai.UnitPrice.value() * (event.Count ? event.Count : 1u);
                event.TimeLeftMin = ai.DurationLeft / 60000u;
                std::string sellerName;
                sCharacterCache->GetCharacterNameByGuid(ai.Owner, sellerName);
                event.Seller = sellerName;
                event.QueryItem = request.ItemEntry;
                event.TotalMatches = total;
                ModuleMgr::instance()->DispatchAuctionListing("PlayerScript.OnAuctionResult", event);
            }

            reply.Success = true;
            TC_LOG_DEBUG("modules.actions", "browsed auctions for item {} ({} listing(s), {} matches)", request.ItemEntry,
                         result.Items.size(), total);
            return;
        }

        case 1: // kBid
        case 2: // kBuyout
        {
            Player* player = primaryPlayer;
            if (!player && request.PlayerGuid)
                player = ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(request.PlayerGuid));
            if (!player)
            {
                reply.Reason = "AuctionHouse bid/buyout has no acting player in the action schema; add a player/buyer guid to AuctionHouseAction";
                return;
            }

            Creature* auctioneer = ResolveAuctioneerCreature(request.AuctioneerGuid);
            if (!auctioneer)
            {
                reply.Reason = "auctioneer not found on any map";
                return;
            }
            Creature* creature = player->GetNPCIfCanInteractWith(auctioneer->GetGUID(), UNIT_NPC_FLAG_AUCTIONEER, UNIT_NPC_FLAG_2_NONE);
            if (!creature)
            {
                reply.Reason = "auctioneer not found or cannot interact with it";
                return;
            }
            AuctionHouseObject* ah = sAuctionMgr->GetAuctionsMap(creature->GetFaction());
            AuctionPosting* auction = ah->GetAuction(static_cast<uint32>(request.AuctionGuid));
            if (!auction)
            {
                reply.Reason = "auction not found";
                return;
            }
            if (auction->Owner == player->GetGUID() || auction->OwnerAccount == player->GetSession()->GetAccountGUID())
            {
                reply.Reason = "cannot bid on your own auction";
                return;
            }

            WorldPackets::AuctionHouse::AuctionPlaceBid placeBid{ WorldPacket(CMSG_AUCTION_PLACE_BID) };
            placeBid.Auctioneer = auctioneer->GetGUID();
            placeBid.AuctionID = static_cast<int32>(request.AuctionGuid);
            uint32 const amount = request.AuctionOp == 2
                                      ? (request.Buyout ? request.Buyout : request.Bid)
                                      : request.Bid;
            placeBid.BidAmount = amount;

            uint64 before = player->GetMoney();
            player->GetSession()->HandleAuctionPlaceBid(placeBid);
            uint64 after = player->GetMoney();
            reply.CopperSpent = before > after ? before - after : 0;
            reply.Success = true;
            reply.AuctionGuid = request.AuctionGuid;
            return;
        }

        case 3: // kList
        {
            Player* player = primaryPlayer;
            if (!player)
            {
                ObjectGuid itemGuid = ObjectGuid::Create<HighGuid::Item>(request.ItemGuid);
                for (auto const& [guid, p] : ObjectAccessor::GetPlayers())
                    if (p && p->GetItemByGuid(itemGuid))
                    {
                        player = p;
                        break;
                    }
            }
            if (!player)
                player = request.PlayerGuid ? ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(request.PlayerGuid)) : nullptr;
            if (!player)
            {
                reply.Reason = "no online player owns the listed item";
                return;
            }

            ObjectGuid itemGuid = ObjectGuid::Create<HighGuid::Item>(request.ItemGuid);
            Item* item = player->GetItemByGuid(itemGuid);
            if (!item)
            {
                reply.Reason = "item is no longer in the player's bags";
                return;
            }
            if (item->GetEntry() != request.ItemEntry && request.ItemEntry != 0)
            {
                reply.Reason = "item entry does not match the request";
                return;
            }

            Creature* auctioneer = ResolveAuctioneerCreature(request.AuctioneerGuid);
            if (!auctioneer)
            {
                reply.Reason = "auctioneer not found on any map";
                return;
            }
            Creature* creature = player->GetNPCIfCanInteractWith(auctioneer->GetGUID(), UNIT_NPC_FLAG_AUCTIONEER, UNIT_NPC_FLAG_2_NONE);
            if (!creature)
            {
                reply.Reason = "auctioneer not found or cannot interact with it";
                return;
            }
            AuctionHouseEntry const* entry = AuctionHouseMgr::GetAuctionHouseEntry(creature->GetFaction(), nullptr);
            if (!entry)
            {
                reply.Reason = "auctioneer has no auction house";
                return;
            }

            uint32 deposit = static_cast<uint32>(AuctionHouseMgr::GetItemAuctionDeposit(player, item, Minutes(120), entry->DepositRate));
            if (!player->HasEnoughMoney(static_cast<uint64>(deposit)))
            {
                reply.Reason = "not enough money for the deposit";
                return;
            }

            WorldPackets::AuctionHouse::AuctionSellItem sellItem{ WorldPacket(CMSG_AUCTION_SELL_ITEM) };
            sellItem.Auctioneer = auctioneer->GetGUID();
            sellItem.MinBid = request.Bid ? request.Bid : 1;
            sellItem.BuyoutPrice = request.Buyout;
            sellItem.RunTime = 120;
            sellItem.Items.push_back({ itemGuid, 1 });

            auto const beforeAuctionId = player->GetSession()->ModuleLastAuctionId;
            (void)beforeAuctionId;
            player->GetSession()->ModuleLastAuctionId = 0;

            uint64 moneyBefore = player->GetMoney();
            player->GetSession()->HandleAuctionSellItem(sellItem);
            uint32 generated = player->GetSession()->ModuleLastAuctionId;

            uint64 moneyAfter = player->GetMoney();
            reply.CopperSpent = moneyBefore > moneyAfter ? moneyBefore - moneyAfter : deposit;
            reply.ItemEntry = item->GetEntry();
            if (generated)
                reply.AuctionGuid = generated;
            reply.Success = true;
            return;
        }

        case 4: // kCancel
        {
            Player* player = primaryPlayer;
            if (!player)
            {
                Creature* auc = ResolveAuctioneerCreature(request.AuctioneerGuid);
                if (auc)
                {
                    AuctionHouseObject* ah = sAuctionMgr->GetAuctionsMap(auc->GetCreatureTemplate()->faction);
                    AuctionPosting* auction = ah->GetAuction(static_cast<uint32>(request.AuctionGuid));
                    if (auction)
                        player = ObjectAccessor::FindConnectedPlayer(auction->Owner);
                }
            }
            if (!player)
            {
                reply.Reason = "no online player owns the auction";
                return;
            }

            Creature* auctioneer = ResolveAuctioneerCreature(request.AuctioneerGuid);
            if (!auctioneer)
            {
                reply.Reason = "auctioneer not found on any map";
                return;
            }
            Creature* creature = player->GetNPCIfCanInteractWith(auctioneer->GetGUID(), UNIT_NPC_FLAG_AUCTIONEER, UNIT_NPC_FLAG_2_NONE);
            if (!creature)
            {
                reply.Reason = "auctioneer not found or cannot interact with it";
                return;
            }
            AuctionHouseObject* ah = sAuctionMgr->GetAuctionsMap(creature->GetFaction());
            AuctionPosting* auction = ah->GetAuction(static_cast<uint32>(request.AuctionGuid));
            if (!auction || auction->Owner != player->GetGUID())
            {
                reply.Reason = "auction not found or not owned by the player";
                return;
            }

            WorldPackets::AuctionHouse::AuctionRemoveItem removeItem{ WorldPacket(CMSG_AUCTION_REMOVE_ITEM) };
            removeItem.Auctioneer = auctioneer->GetGUID();
            removeItem.AuctionID = static_cast<int32>(request.AuctionGuid);

            uint64 before = player->GetMoney();
            player->GetSession()->HandleAuctionRemoveItem(removeItem);
            uint64 after = player->GetMoney();
            reply.CopperSpent = before > after ? before - after : 0;
            reply.Success = true;
            reply.AuctionGuid = request.AuctionGuid;
            return;
        }
    }

    reply.Reason = "unknown auction operation";
}

} // namespace

void ExecuteModuleAction(ModuleWire::ActionRequestMsg const& request, ModuleWire::ActionReplyMsg& reply, std::string const& moduleName,
                         uint32 module)
{
    reply.MessageId = request.MessageId;

    switch (request.Kind)
    {
        case ModuleWire::ActionKind::StoreGet:
        case ModuleWire::ActionKind::StoreSet:
        case ModuleWire::ActionKind::StoreDelete:
        case ModuleWire::ActionKind::StoreList:
            if (moduleName.empty())
                reply.Reason = "module is no longer loaded";
            else
                ExecuteStoreAction(moduleName, request, reply);
            return;

        case ModuleWire::ActionKind::BuyItem:
            BuyItemAction(nullptr, request, reply);
            return;

        case ModuleWire::ActionKind::AuctionHouse:
            AuctionHouseAction(nullptr, request, reply);
            return;

        case ModuleWire::ActionKind::BotSpeak:
            ModuleBotTrade::ExecuteSpeak(request, reply, module);
            return;

        case ModuleWire::ActionKind::BotTrade:
            ModuleBotTrade::ExecuteTrade(request, reply, module);
            return;

        case ModuleWire::ActionKind::Log:
            if (request.Text.empty() || request.Text.size() > MaxMessageBytes)
            {
                reply.Reason = "log text must be 1 to 255 bytes";
                return;
            }
            switch (request.LogLevel)
            {
                case 0: TC_LOG_INFO("server.worldserver", "[{}] {}", moduleName, request.Text); break;
                case 1: TC_LOG_WARN("server.worldserver", "[{}] {}", moduleName, request.Text); break;
                default: TC_LOG_ERROR("server.worldserver", "[{}] {}", moduleName, request.Text); break;
            }
            reply.Success = true;
            return;

        default:
            break;
    }

    Player* player = ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(request.PlayerGuid));
    if (!player || !player->GetSession())
    {
        reply.Reason = "player is not online";
        return;
    }

    switch (request.Kind)
    {
        case ModuleWire::ActionKind::SellItem:
            SellItem(player, request, reply);
            return;

        case ModuleWire::ActionKind::BuyItem:
            BuyItem(player, request, reply);
            return;

        case ModuleWire::ActionKind::AuctionHouse:
            AuctionHouse(player, request, reply);
            return;

        case ModuleWire::ActionKind::ShareQuestItem:
            ShareQuestItem(player, request, reply);
            return;

        case ModuleWire::ActionKind::SendPlayerMessage:
            if (request.Text.empty() || request.Text.size() > MaxMessageBytes)
            {
                reply.Reason = "message must be 1 to 255 bytes";
                return;
            }
            ChatHandler(player->GetSession()).SendSysMessage(request.Text);
            reply.Success = true;
            return;

        case ModuleWire::ActionKind::Log:
        case ModuleWire::ActionKind::BotSpeak:
        case ModuleWire::ActionKind::BotTrade:
        case ModuleWire::ActionKind::None:
            break;
    }

    reply.Reason = "unknown action";
}

#include "ModuleBotSupplies.h"
#include "ConditionMgr.h"
#include "Config.h"
#include "Creature.h"
#include "CreatureData.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "PathGenerator.h"
#include "Player.h"
#include "SharedDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

#include <algorithm>
#include <map>
#include <utility>

namespace ModuleBotSupplies
{
bool VendorStock::SellsAny() const
{
    for (uint8 level : MinLevel)
        if (level != kNoStock)
            return true;
    return false;
}

bool Enabled()
{
    static bool const enabled = sConfigMgr->GetBoolDefault("Bot.Actions.Supplies", true);
    return enabled;
}

uint32 ReservePerLevel()
{
    static uint32 const value = sConfigMgr->GetIntDefault("Bot.Supplies.ReserveCopperPerLevel", 5);
    return value;
}

uint32 HealthPotionTarget()
{
    static uint32 const value = sConfigMgr->GetIntDefault("Bot.Supplies.HealthPotionTarget", 3);
    return value;
}

uint32 ManaPotionTarget()
{
    static uint32 const value = sConfigMgr->GetIntDefault("Bot.Supplies.ManaPotionTarget", 3);
    return value;
}

uint32 BandageTarget()
{
    static uint32 const value = sConfigMgr->GetIntDefault("Bot.Supplies.BandageTarget", 10);
    return value;
}

namespace
{
// A potion by what its use spell does: a heal, or restoring mana.
Kind PotionKind(ItemTemplate const* proto)
{
    if (proto->Effects.empty())
        return Kind::None;
    SpellInfo const* info = sSpellMgr->GetSpellInfo(uint32(proto->Effects[0]->SpellID), DIFFICULTY_NONE);
    if (!info)
        return Kind::None;
    for (SpellEffectInfo const& effect : info->GetEffects())
    {
        if (effect.Effect == SPELL_EFFECT_HEAL || effect.Effect == SPELL_EFFECT_HEAL_PCT)
            return Kind::HealthPotion;
        if ((effect.Effect == SPELL_EFFECT_ENERGIZE || effect.Effect == SPELL_EFFECT_ENERGIZE_PCT) && effect.MiscValue == POWER_MANA)
            return Kind::ManaPotion;
    }
    return Kind::None;
}
}

Kind KindOf(ItemTemplate const* proto)
{
    if (!proto || proto->GetClass() != ITEM_CLASS_CONSUMABLE || proto->Effects.empty())
        return Kind::None;
    switch (proto->GetSubClass())
    {
        case ITEM_SUBCLASS_FOOD_DRINK:
            switch (proto->Effects[0]->SpellCategoryID)
            {
                case SPELL_CATEGORY_FOOD: return Kind::Food;
                case SPELL_CATEGORY_DRINK: return Kind::Drink;
                default: return Kind::None;
            }
        case ITEM_SUBCLASS_POTION:
            return PotionKind(proto);
        case ITEM_SUBCLASS_BANDAGE:
            return Kind::Bandage;
        default:
            return Kind::None;
    }
}

namespace
{
uint8 ClampLevel(int32 level)
{
    return uint8(std::clamp<int32>(level, 0, kNoStock - 1));
}

// A vendor's item a bot could buy with money: no other cost, no reputation, no limited stock.
bool BuyableWithMoney(VendorItem const& vendorItem, ItemTemplate const* proto)
{
    return proto && vendorItem.Type == ITEM_VENDOR_TYPE_ITEM && vendorItem.ExtendedCost == 0 && vendorItem.maxcount == 0 &&
        proto->GetRequiredReputationFaction() == 0 && proto->GetBuyPrice() > 0;
}

// How many of `entry` fit in the bags, up to `count`: the bags' room is not a simple number (partial stacks), so it is asked.
uint32 FitCount(Player& bot, uint32 entry, uint32 count)
{
    ItemPosCountVec destination;
    if (bot.CanStoreNewItem(NULL_BAG, NULL_SLOT, destination, entry, count) == EQUIP_ERR_OK)
        return count;
    uint32 low = 0, high = count;
    while (low < high)
    {
        uint32 const mid = low + (high - low + 1) / 2;
        destination.clear();
        if (bot.CanStoreNewItem(NULL_BAG, NULL_SLOT, destination, entry, mid) == EQUIP_ERR_OK)
            low = mid;
        else
            high = mid - 1;
    }
    return low;
}

// The vendor item the bot should buy of one kind, or nullptr: the best the bot may use at its level. slot is its place on the vendor's list.
struct Pick
{
    VendorItem const* Item = nullptr;
    ItemTemplate const* Proto = nullptr;
    uint32 Slot = 0;
};

enum class Want : uint8
{
    Ammo,
    Food,
    Drink,
    HealthPotion,
    ManaPotion,
    Bandage
};

Kind KindOfWant(Want want)
{
    switch (want)
    {
        case Want::Food: return Kind::Food;
        case Want::Drink: return Kind::Drink;
        case Want::HealthPotion: return Kind::HealthPotion;
        case Want::ManaPotion: return Kind::ManaPotion;
        case Want::Bandage: return Kind::Bandage;
        default: return Kind::None;
    }
}

Pick BestAt(Player& bot, Creature* vendor, Want want)
{
    Pick best;
    VendorItemData const* items = vendor->GetVendorItems();
    if (!items)
        return best;

    int32 const ammoSubclass = AmmoSubclassOf(bot);
    for (uint32 slot = 0; slot < items->GetItemCount(); ++slot)
    {
        VendorItem const* vendorItem = items->GetItem(slot);
        ItemTemplate const* proto = vendorItem ? sObjectMgr->GetItemTemplate(vendorItem->item) : nullptr;
        if (!vendorItem || !BuyableWithMoney(*vendorItem, proto))
            continue;
        if (proto->GetBaseRequiredLevel() > int32(bot.GetLevel()))
            continue;

        if (want == Want::Ammo)
        {
            if (ammoSubclass < 0 || proto->GetClass() != ITEM_CLASS_PROJECTILE || int32(proto->GetSubClass()) != ammoSubclass ||
                bot.CanUseItem(proto) != EQUIP_ERR_OK)
                continue;
        }
        else
        {
            Kind const kind = KindOfWant(want);
            if (KindOf(proto) != kind || !Usable(bot, proto, kind))
                continue;
        }
        if (!sConditionMgr->IsObjectMeetingVendorItemConditions(vendor->GetEntry(), vendorItem->item, &bot, vendor) ||
            !ConditionMgr::IsPlayerMeetingCondition(&bot, vendorItem->PlayerConditionId))
            continue;

        // Better: a higher required level, then a dearer item (it restores or hits more), then the lower entry so the choice is stable.
        bool better = !best.Proto;
        if (!better)
        {
            if (proto->GetBaseRequiredLevel() != best.Proto->GetBaseRequiredLevel())
                better = proto->GetBaseRequiredLevel() > best.Proto->GetBaseRequiredLevel();
            else if (proto->GetBuyPrice() != best.Proto->GetBuyPrice())
                better = proto->GetBuyPrice() > best.Proto->GetBuyPrice();
            else
                better = proto->GetId() < best.Proto->GetId();
        }
        if (better)
            best = { vendorItem, proto, slot };
    }
    return best;
}

// Buys up to `need` more of the picked item, within the bot's money (less the reserve) and bag room. Returns how many it bought.
uint32 BuyMore(Player& bot, Creature* vendor, Pick const& pick, uint32 need, uint64& copperSpent, uint64 reserve)
{
    if (!pick.Proto || need == 0)
        return 0;

    // The price is per BuyCount pieces; rounding up and ignoring discounts keeps the estimate on the safe side.
    uint64 const perPiece = (uint64(pick.Proto->GetBuyPrice()) + pick.Proto->GetBuyCount() - 1) / pick.Proto->GetBuyCount();
    uint32 count = need;
    if (perPiece > 0)
        count = uint32(std::min<uint64>(count, Spendable(bot.GetMoney(), reserve) / perPiece));
    count = FitCount(bot, pick.Proto->GetId(), count);

    // A refusal is most often the money (a discount or a rounding the estimate missed): try a smaller number a few times.
    for (uint32 attempt = 0; count > 0 && attempt < 3; ++attempt, count = count * 3 / 4)
    {
        uint64 const before = bot.GetMoney();
        uint32 const carried = bot.GetItemCount(pick.Proto->GetId());
        if (!bot.BuyItemFromVendorSlot(vendor->GetGUID(), pick.Slot, pick.Proto->GetId(), count, NULL_BAG, NULL_SLOT))
            continue;
        copperSpent += before > bot.GetMoney() ? before - bot.GetMoney() : 0;
        uint32 const now = bot.GetItemCount(pick.Proto->GetId());
        return now > carried ? now - carried : 0;
    }
    return 0;
}
}

VendorStock StockOf(uint32 vendorEntry)
{
    VendorStock stock;
    VendorItemData const* items = sObjectMgr->GetNpcVendorItemList(vendorEntry);
    if (!items)
        return stock;

    for (uint32 slot = 0; slot < items->GetItemCount(); ++slot)
    {
        VendorItem const* vendorItem = items->GetItem(slot);
        ItemTemplate const* proto = vendorItem ? sObjectMgr->GetItemTemplate(vendorItem->item) : nullptr;
        if (!vendorItem || !BuyableWithMoney(*vendorItem, proto))
            continue;

        int32 index = -1;
        if (proto->GetClass() == ITEM_CLASS_PROJECTILE)
        {
            if (proto->GetSubClass() == ITEM_SUBCLASS_ARROW)
                index = StockArrows;
            else if (proto->GetSubClass() == ITEM_SUBCLASS_BULLET)
                index = StockBullets;
        }
        else if (Kind const kind = KindOf(proto); kind == Kind::Food)
            index = StockFood;
        else if (kind == Kind::Drink)
            index = StockDrink;
        else if (kind == Kind::HealthPotion)
            index = StockHealthPotion;
        else if (kind == Kind::ManaPotion)
            index = StockManaPotion;
        else if (kind == Kind::Bandage)
            index = StockBandage;
        if (index < 0)
            continue;
        stock.MinLevel[index] = std::min(stock.MinLevel[index], ClampLevel(proto->GetBaseRequiredLevel()));
    }
    return stock;
}

int32 AmmoSubclassOf(Player const& bot)
{
    Item const* ranged = bot.GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED);
    if (!ranged || ranged->GetTemplate()->GetClass() != ITEM_CLASS_WEAPON)
        return -1;
    switch (ranged->GetTemplate()->GetSubClass())
    {
        case ITEM_SUBCLASS_WEAPON_BOW:
        case ITEM_SUBCLASS_WEAPON_CROSSBOW:
            return ITEM_SUBCLASS_ARROW;
        case ITEM_SUBCLASS_WEAPON_GUN:
            return ITEM_SUBCLASS_BULLET;
        default:
            return -1;
    }
}

bool Usable(Player const& bot, ItemTemplate const* proto, Kind kind)
{
    if (!proto || kind == Kind::None || KindOf(proto) != kind)
        return false;
    if (proto->GetBaseRequiredLevel() > int32(bot.GetLevel()) || bot.CanUseItem(proto) != EQUIP_ERR_OK)
        return false;
    return KindFitsClass(kind, bot.GetMaxPower(POWER_MANA) > 0);
}

bool UsableFoodOrDrink(Player const& bot, ItemTemplate const* proto, Kind kind)
{
    return (kind == Kind::Food || kind == Kind::Drink) && Usable(bot, proto, kind);
}

uint32 AmmoEntryFor(Player& bot)
{
    int32 const subclass = AmmoSubclassOf(bot);
    if (subclass < 0)
        return 0;

    auto const fits = [&](ItemTemplate const* proto)
    {
        return proto && proto->GetClass() == ITEM_CLASS_PROJECTILE && int32(proto->GetSubClass()) == subclass &&
            proto->GetBaseRequiredLevel() <= int32(bot.GetLevel()) && bot.CanUseItem(proto) == EQUIP_ERR_OK;
    };

    if (uint32 const selected = bot.GetAmmoId(); selected && bot.HasItemCount(selected) && fits(sObjectMgr->GetItemTemplate(selected)))
        return selected;

    uint32 carried = 0;
    int32 carriedLevel = -1;
    bot.ForEachItem(ItemSearchLocation::Inventory, [&](Item* item)
    {
        ItemTemplate const* proto = item->GetTemplate();
        if (fits(proto) && proto->GetBaseRequiredLevel() > carriedLevel)
        {
            carried = proto->GetId();
            carriedLevel = proto->GetBaseRequiredLevel();
        }
        return ItemSearchCallbackResult::Continue;
    });
    if (carried)
        return carried;

    // Nothing carried: the best ammunition of its kind at the bot's level, from the item data. Worked out once per kind and level.
    static std::map<std::pair<int32, uint8>, uint32> recommended;
    auto const key = std::make_pair(subclass, uint8(std::min<uint32>(bot.GetLevel(), 255)));
    if (auto const known = recommended.find(key); known != recommended.end())
        return known->second;

    ItemTemplate const* best = nullptr;
    for (auto const& [entry, proto] : sObjectMgr->GetItemTemplateStore())
    {
        if (proto.GetClass() != ITEM_CLASS_PROJECTILE || int32(proto.GetSubClass()) != subclass ||
            proto.GetBaseRequiredLevel() > int32(bot.GetLevel()) || bot.CanUseItem(&proto) != EQUIP_ERR_OK)
            continue;
        if (!best || proto.GetBaseRequiredLevel() > best->GetBaseRequiredLevel() ||
            (proto.GetBaseRequiredLevel() == best->GetBaseRequiredLevel() && entry < best->GetId()))
            best = &proto;
    }
    return recommended[key] = best ? best->GetId() : 0;
}

uint32 CountCarried(Player& bot, Kind kind)
{
    uint32 total = 0;
    bot.ForEachItem(ItemSearchLocation::Inventory, [&](Item* item)
    {
        if (Usable(bot, item->GetTemplate(), kind))
            total += item->GetCount();
        return ItemSearchCallbackResult::Continue;
    });
    return total;
}

Carried CountAllCarried(Player& bot)
{
    Carried carried;
    bool const mana = bot.GetMaxPower(POWER_MANA) > 0;
    bot.ForEachItem(ItemSearchLocation::Inventory, [&](Item* item)
    {
        ItemTemplate const* proto = item->GetTemplate();
        Kind const kind = KindOf(proto);
        if (kind == Kind::None || !Usable(bot, proto, kind))
            return ItemSearchCallbackResult::Continue;
        switch (kind)
        {
            case Kind::Food: carried.Food += item->GetCount(); break;
            case Kind::Drink: carried.Drink += mana ? item->GetCount() : 0; break;
            case Kind::HealthPotion: carried.HealthPotions += item->GetCount(); break;
            case Kind::ManaPotion: carried.ManaPotions += mana ? item->GetCount() : 0; break;
            case Kind::Bandage: carried.Bandages += item->GetCount(); break;
            default: break;
        }
        return ItemSearchCallbackResult::Continue;
    });
    return carried;
}

bool RouteLooksOpen(Player& bot, float x, float y, float z)
{
    PathGenerator path(&bot);
    if (!path.CalculatePath(x, y, z, false))
        return false;
    PathType const type = path.GetPathType();
    if (type & PATHFIND_NOT_USING_PATH)
        return true;   // no navmesh to ask (no mmaps for the map): a straight line is how the bot moves there
    if (type & PATHFIND_NOPATH)
        return false;
    if (type & PATHFIND_INCOMPLETE)
    {
        // A partial route is normal for a long walk (the pathfinder looks a limited number of polygons ahead). It is a bad sign only for a place
        // near enough that a whole route would fit: then the mesh really runs out (a fence, a river, a cliff).
        float const dx = x - bot.GetPositionX();
        float const dy = y - bot.GetPositionY();
        constexpr float kFullRouteYards = 250.0f;
        if (dx * dx + dy * dy > kFullRouteYards * kFullRouteYards)
            return true;
        G3D::Vector3 const end = path.GetActualEndPosition();
        float const ex = x - end.x;
        float const ey = y - end.y;
        constexpr float kCloseEnoughYards = 25.0f;
        return ex * ex + ey * ey <= kCloseEnoughYards * kCloseEnoughYards;
    }
    return true;
}

Item* BestCarried(Player& bot, Kind kind)
{
    Item* best = nullptr;
    bot.ForEachItem(ItemSearchLocation::Inventory, [&](Item* item)
    {
        ItemTemplate const* proto = item->GetTemplate();
        if (!Usable(bot, proto, kind))
            return ItemSearchCallbackResult::Continue;
        // The best is the highest required level (it restores most); a smaller stack is eaten first so the stacks do not linger.
        if (!best || proto->GetBaseRequiredLevel() > best->GetTemplate()->GetBaseRequiredLevel() ||
            (proto->GetBaseRequiredLevel() == best->GetTemplate()->GetBaseRequiredLevel() && item->GetCount() < best->GetCount()))
            best = item;
        return ItemSearchCallbackResult::Continue;
    });
    return best;
}

Bought Buy(Player& bot, Creature* vendor, Targets const& targets)
{
    Bought bought;
    if (!vendor || !vendor->IsInWorld() || !bot.IsAlive())
        return bought;

    if (targets.Ammo > 0 && AmmoSubclassOf(bot) >= 0)
    {
        Pick const pick = BestAt(bot, vendor, Want::Ammo);
        if (pick.Proto)
        {
            uint32 const carried = bot.GetItemCount(pick.Proto->GetId());
            bought.Ammo = BuyMore(bot, vendor, pick, targets.Ammo > carried ? targets.Ammo - carried : 0, bought.Copper, 0);
            // Ammunition the bot shoots now is replaced by the better kind it just bought; with none selected the new one is selected.
            if (bought.Ammo > 0)
            {
                ItemTemplate const* current = bot.GetAmmoId() ? sObjectMgr->GetItemTemplate(bot.GetAmmoId()) : nullptr;
                if (!current || !bot.HasItemCount(current->GetId()) || current->GetBaseRequiredLevel() < pick.Proto->GetBaseRequiredLevel())
                    bot.SetAmmo(pick.Proto->GetId());
            }
        }
    }
    // Everything else keeps the reserve back: money for repairs and the next trip is worth more than a few more pieces.
    uint64 const reserve = ReserveFor(bot.GetLevel(), ReservePerLevel());
    bool const mana = bot.GetMaxPower(POWER_MANA) > 0;
    auto const buyKind = [&](Want want, Kind kind, uint32 target, uint32& result)
    {
        if (target == 0)
            return;
        Pick const pick = BestAt(bot, vendor, want);
        uint32 const carried = CountCarried(bot, kind);
        result = BuyMore(bot, vendor, pick, target > carried ? target - carried : 0, bought.Copper, reserve);
    };
    buyKind(Want::Food, Kind::Food, targets.Food, bought.Food);
    if (mana)
        buyKind(Want::Drink, Kind::Drink, targets.Drink, bought.Drink);
    buyKind(Want::HealthPotion, Kind::HealthPotion, targets.HealthPotion, bought.HealthPotion);
    if (mana)
        buyKind(Want::ManaPotion, Kind::ManaPotion, targets.ManaPotion, bought.ManaPotion);
    buyKind(Want::Bandage, Kind::Bandage, targets.Bandage, bought.Bandage);

    if (bought.Ammo || bought.Food || bought.Drink || bought.HealthPotion || bought.ManaPotion || bought.Bandage)
        TC_LOG_DEBUG("server.worldserver", "Bots: {} bought {} ammunition, {} food, {} drink, {} health potions, {} mana potions and {} bandages for {} copper.",
            bot.GetName(), bought.Ammo, bought.Food, bought.Drink, bought.HealthPotion, bought.ManaPotion, bought.Bandage, bought.Copper);
    return bought;
}
}

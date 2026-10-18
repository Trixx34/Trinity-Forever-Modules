/*
 * Bot supplies (layout 15): ammunition, food and drink. A bot has no client, so it shops and eats on a module's say-so:
 *
 *  - the snapshot tells the module what the bot carries (ammo_entry/ammo_count, food_count, drink_count) and where the nearest vendor is
 *    that sells what the bot can use (ammo_vendor_*, food_vendor_*);
 *  - kVendor with the buy bits (8 ammunition, 16 food, 32 drink) makes the bot buy from the vendor it stands at, up to the target counts the
 *    module gives. The item is chosen here, from the vendor's stock, because the item data lives in the core;
 *  - kConsume makes the bot eat or drink the best food or drink it carries.
 *
 * Setting: Bot.Actions.Supplies (default 1) allows buying and consuming.
 */
#pragma once

#include "Define.h"

#include <string>

class Creature;
class Item;
class ItemTemplate;
class Player;

namespace ModuleBotSupplies
{
enum class Kind : uint8
{
    None = 0,
    Food = 1,
    Drink = 2
};

// Indexes of VendorStock::MinLevel.
enum StockIndex : uint8
{
    StockArrows = 0,
    StockBullets = 1,
    StockFood = 2,
    StockDrink = 3,
    StockCount = 4
};

// What a vendor template sells, as the lowest required level of each kind (kNoStock when it sells none). Items that cost something other
// than money, or that need reputation, do not count: a bot cannot buy them.
constexpr uint8 kNoStock = 255;
struct VendorStock
{
    uint8 MinLevel[StockCount] = { kNoStock, kNoStock, kNoStock, kNoStock };
    bool SellsAny() const;
};

bool Enabled();

// Food or drink, from the spell category of the item's use effect (the way the core's own starting-item code tells them apart).
Kind KindOf(ItemTemplate const* proto);

// The lowest required levels of what the vendor template with this entry sells.
VendorStock StockOf(uint32 vendorEntry);

// The ammunition the bot's ranged weapon takes: ITEM_SUBCLASS_ARROW, ITEM_SUBCLASS_BULLET, or -1 when it uses none.
int32 AmmoSubclassOf(Player const& bot);

// Whether the bot may use this food or drink now: its level, and mana for a drink.
bool UsableFoodOrDrink(Player const& bot, ItemTemplate const* proto, Kind kind);

// The ammunition the bot shoots: the one selected when it is carried and fits, else the best carried that fits, else the best at its level
// (so a hunter that carries none still names what it needs). 0 when the bot uses no ammunition.
uint32 AmmoEntryFor(Player& bot);

// How many pieces of usable food or drink the bot carries in its bags.
uint32 CountCarried(Player& bot, Kind kind);

// What a kVendor buy asks for: counts to bring the bot up to (0 = none of that kind).
struct Targets
{
    uint32 Ammo = 0;
    uint32 Food = 0;
    uint32 Drink = 0;
};

struct Bought
{
    uint32 Ammo = 0;
    uint32 Food = 0;
    uint32 Drink = 0;
    uint64 Copper = 0;
};

// Buys at `vendor` (the bot stands in interaction range of it) for each kind with a target above what the bot carries. Ammunition first,
// then food, then drink, each from what is left of the bot's money. Buys less when the money or the bags run out.
Bought Buy(Player& bot, Creature* vendor, Targets const& targets);

// The best food or drink the bot carries and may use, or nullptr.
Item* BestCarried(Player& bot, Kind kind);
}

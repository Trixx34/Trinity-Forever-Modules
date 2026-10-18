/*
 * Bot supplies (layout 15 and 16): ammunition, food, drink, health and mana potions, bandages. A bot has no client, so it shops and eats on a module's say-so:
 *
 *  - the snapshot tells the module what the bot carries (ammo_entry/ammo_count, food_count, drink_count) and where the nearest vendor is
 *    that sells what the bot can use (ammo_vendor_*, food_vendor_*);
 *  - kVendor with the buy bits (8 ammunition, 16 food, 32 drink) makes the bot buy from the vendor it stands at, up to the target counts the
 *    module gives. The item is chosen here, from the vendor's stock, because the item data lives in the core;
 *  - kConsume makes the bot eat or drink the best food or drink it carries (layout 16: or use a potion or a bandage).
 *
 * Settings: Bot.Actions.Supplies (default 1) allows buying and consuming. Layout 16 buying keeps a money reserve back
 * (Bot.Supplies.ReserveCopperPerLevel) from everything but ammunition, and buys up to Bot.Supplies.HealthPotionTarget, ManaPotionTarget and
 * BandageTarget pieces.
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
    Drink = 2,
    HealthPotion = 3,   // layout 16
    ManaPotion = 4,
    Bandage = 5
};

// Indexes of VendorStock::MinLevel.
enum StockIndex : uint8
{
    StockArrows = 0,
    StockBullets = 1,
    StockFood = 2,
    StockDrink = 3,
    StockHealthPotion = 4,   // layout 16
    StockManaPotion = 5,
    StockBandage = 6,
    StockCount = 7
};

// What a vendor template sells, as the lowest required level of each kind (kNoStock when it sells none). Items that cost something other
// than money, or that need reputation, do not count: a bot cannot buy them.
constexpr uint8 kNoStock = 255;
struct VendorStock
{
    uint8 MinLevel[StockCount] = { kNoStock, kNoStock, kNoStock, kNoStock, kNoStock, kNoStock, kNoStock };
    bool SellsAny() const;
};

bool Enabled();

// Food or drink (from the spell category of the item's use effect, the way the core's own starting-item code tells them apart), a potion
// that heals or restores mana (layout 16: from the effect of its use spell), or a bandage.
Kind KindOf(ItemTemplate const* proto);

// The money a bot keeps back when it shops for anything but ammunition: `perLevel` copper for each level it has. Pure arithmetic, so the
// settings can be tested without a world.
constexpr uint64 ReserveFor(uint32 level, uint32 perLevel)
{
    return uint64(level) * perLevel;
}

// What of `money` may be spent when `reserve` is kept back.
constexpr uint64 Spendable(uint64 money, uint64 reserve)
{
    return money > reserve ? money - reserve : 0;
}

static_assert(ReserveFor(60, 5) == 300 && ReserveFor(1, 0) == 0, "the reserve is perLevel copper for each level");
static_assert(Spendable(1000, 300) == 700 && Spendable(300, 300) == 0 && Spendable(100, 300) == 0, "money under the reserve is not spendable");

// The settings (read once from worldserver.conf).
uint32 ReservePerLevel();   // Bot.Supplies.ReserveCopperPerLevel, default 5
uint32 HealthPotionTarget(); // Bot.Supplies.HealthPotionTarget, default 3
uint32 ManaPotionTarget();   // Bot.Supplies.ManaPotionTarget, default 3
uint32 BandageTarget();      // Bot.Supplies.BandageTarget, default 10

// Whether a bot with `maxMana` mana can use an item of this kind at all (a mana potion needs mana).
constexpr bool KindFitsClass(Kind kind, bool manaUser)
{
    return kind != Kind::Drink && kind != Kind::ManaPotion ? true : manaUser;
}

// The lowest required levels of what the vendor template with this entry sells.
VendorStock StockOf(uint32 vendorEntry);

// The ammunition the bot's ranged weapon takes: ITEM_SUBCLASS_ARROW, ITEM_SUBCLASS_BULLET, or -1 when it uses none.
int32 AmmoSubclassOf(Player const& bot);

// Whether the bot may use this item of `kind` now: its level, and mana for a drink or a mana potion.
bool Usable(Player const& bot, ItemTemplate const* proto, Kind kind);
bool UsableFoodOrDrink(Player const& bot, ItemTemplate const* proto, Kind kind);

// The ammunition the bot shoots: the one selected when it is carried and fits, else the best carried that fits, else the best at its level
// (so a hunter that carries none still names what it needs). 0 when the bot uses no ammunition.
uint32 AmmoEntryFor(Player& bot);

// How many pieces of usable food or drink the bot carries in its bags.
uint32 CountCarried(Player& bot, Kind kind);

// Everything the snapshot reports, counted in one walk through the bags.
struct Carried
{
    uint32 Food = 0;
    uint32 Drink = 0;
    uint32 HealthPotions = 0;
    uint32 ManaPotions = 0;
    uint32 Bandages = 0;
};
Carried CountAllCarried(Player& bot);

// Whether the navmesh gives the bot a way to (x, y, z) from where it stands, as far as can be told from here: false when there is no route at
// all, or when the route stops well short of a place that is close enough for a full route to exist. A long walk is accepted, since the
// pathfinder only looks a limited way ahead and the bot finishes the route in stages. Costs a path calculation: callers ration them.
bool RouteLooksOpen(Player& bot, float x, float y, float z);

// What a kVendor buy asks for: counts to bring the bot up to (0 = none of that kind).
struct Targets
{
    uint32 Ammo = 0;
    uint32 Food = 0;
    uint32 Drink = 0;
    uint32 HealthPotion = 0;   // layout 16
    uint32 ManaPotion = 0;
    uint32 Bandage = 0;
};

struct Bought
{
    uint32 Ammo = 0;
    uint32 Food = 0;
    uint32 Drink = 0;
    uint32 HealthPotion = 0;
    uint32 ManaPotion = 0;
    uint32 Bandage = 0;
    uint64 Copper = 0;
};

// Buys at `vendor` (the bot stands in interaction range of it) for each kind with a target above what the bot carries. Ammunition first
// (it may spend all the bot's money), then food, drink, health potions, mana potions and bandages, each from what is left of the bot's money
// above the reserve. Buys less when the money or the bags run out.
Bought Buy(Player& bot, Creature* vendor, Targets const& targets);

// The best food or drink the bot carries and may use, or nullptr.
Item* BestCarried(Player& bot, Kind kind);
}

#include "ModuleBotActions.h"
#include "ModuleGroupRules.h"
#include "ModuleMgr.h"
#include "BotProfessions.h"
#include "BotTaxi.h"
#include "BotGearRules.h"
#include "ModuleBotSnapshot.h"
#include "ModuleBotSupplies.h"
#include <cmath>
#include <limits>
#include "BotMgr.h"
#include "Cell.h"
#include "CharmInfo.h"
#include "Corpse.h"
#include "GameTime.h"
#include "InstanceScript.h"
#include "MiscPackets.h"
#include "MapManager.h"
#include "Config.h"
#include "CellImpl.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "DB2Structure.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Group.h"
#include "GroupMgr.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "Loot.h"
#include "LootPackets.h"
#include "Map.h"
#include "MotionMaster.h"
#include "Opcodes.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "PathGenerator.h"
#include "Pet.h"
#include "PetAI.h"
#include "Player.h"
#include "QuestDef.h"
#include "SpellHistory.h"
#include "SpellInfo.h"
#include "DB2Stores.h"
#include "TaxiPathGraph.h"
#include "SpellMgr.h"
#include "Trainer.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <algorithm>
#include <list>
#include <cctype>
#include <limits>
#include <map>
#include <set>
#include <vector>

using forever_module::ActionType;
using forever_module::BotAction;
using forever_module::BotActionStatus;

namespace
{

// Resolves a unit reference (forever_module::MakeUnitRef) on the bot's map. Players are found anywhere online.
// Creatures are found only on the bot's map, since a creature guid is meaningless elsewhere.
Unit* ResolveUnit(Player& bot, uint64 ref)
{
    switch (static_cast<HighGuid>(forever_module::UnitRefType(ref)))
    {
        case HighGuid::Player:
            return ObjectAccessor::FindPlayerByLowGUID(forever_module::UnitRefCounter(ref));

        case HighGuid::Creature:
        {
            Map* map = bot.GetMap();
            if (!map)
                return nullptr;
            ObjectGuid const guid = ObjectGuid::Create<HighGuid::Creature>(uint16(map->GetId()),
                                                                          forever_module::UnitRefEntry(ref),
                                                                          forever_module::UnitRefCounter(ref));
            return map->GetCreature(guid);
        }

        default:
            return nullptr;
    }
}

// A target the bot may attack: alive, in the world, and hostile to the bot.
Unit* ResolveHostileTarget(Player& bot, uint64 ref, std::string& reason)
{
    Unit* target = ResolveUnit(bot, ref);
    if (!target || target == &bot || !target->IsInWorld() || !target->IsAlive())
    {
        reason = "target is not a live unit on this bot's map";
        return nullptr;
    }
    if (!BotMayFight(bot, *target))
    {
        reason = "target is not one the bot may attack (friendly, a critter, an NPC, or not attackable)";
        return nullptr;
    }
    return target;
}

// Why Unit::Attack refused an attack, for the log. The checks follow the ones Unit::Attack makes before it starts an attack.
std::string AttackRefusalReason(Player& bot, Unit* target)
{
    if (!bot.IsAlive())
        return "bot is dead";
    if (!target->IsInWorld() || !target->IsAlive())
        return "target is dead or not in the world";
    if (bot.IsMounted())
        return "bot is mounted";
    if (target->IsPlayer() && target->ToPlayer()->IsGameMaster())
        return "target is a game master";
    if (target->IsCreature() && target->ToCreature()->IsEvadingAttacks())
        return "target is evading attacks";
    return "the core refused the attack for no known reason";
}

// Maps a cast result to the status a module sees in the next snapshot.
BotActionStatus StatusForCast(SpellCastResult result)
{
    switch (result)
    {
        case SPELL_CAST_OK:
            return BotActionStatus::kApplied;
        case SPELL_FAILED_OUT_OF_RANGE:
            return BotActionStatus::kOutOfRange;
        case SPELL_FAILED_NOT_READY:
            return BotActionStatus::kOnCooldown;
        case SPELL_FAILED_NO_POWER:
            return BotActionStatus::kNotEnoughPower;
        case SPELL_FAILED_LINE_OF_SIGHT:
            return BotActionStatus::kNoLineOfSight;
        case SPELL_FAILED_BAD_TARGETS:
        case SPELL_FAILED_BAD_IMPLICIT_TARGETS:
            return BotActionStatus::kInvalidTarget;
        case SPELL_FAILED_NOT_KNOWN:
            return BotActionStatus::kNotKnown;
        case SPELL_FAILED_MOVING:
            return BotActionStatus::kMoving;
        case SPELL_FAILED_SPELL_IN_PROGRESS:
            return BotActionStatus::kBusy;
        case SPELL_FAILED_REQUIRES_SPELL_FOCUS:
            return BotActionStatus::kNeedsFocus; // an anvil, a forge or a fire is not near: the module walks to one
        default:
            return BotActionStatus::kRefused;
    }
}

} // namespace

// The highest rank of a spell the bot knows, walking the rank chain from the id the module sent (its rank 1). 0 if the bot
// knows no rank of it. A lower rank that was replaced by a higher one is still in the chain, so this finds the higher one.
uint32 HighestKnownRank(Player& bot, uint32 spellId)
{
    uint32 best = bot.HasSpell(spellId) ? spellId : 0;
    uint32 id = spellId;
    for (uint32 guard = 0; guard < 16; ++guard)
    {
        uint32 const next = sSpellMgr->GetNextSpellInChain(id);
        if (!next)
            break;
        if (bot.HasSpell(next))
            best = next;
        id = next;
    }
    return best;
}

// Services at NPCs (M3, M4). The NPC must be within interaction range, the distance the core's own NPC checks use, so the
// bot only looks at NPCs the core would serve. Walking to a service is travel (M6), not done here.
constexpr uint32 kServiceSellBit = 1;   // spell_id bit: sell grey items to a vendor
constexpr uint32 kServiceRepairBit = 2; // spell_id bit: repair at a repairman
constexpr uint32 kServiceEquipBit = 4;  // spell_id bit: equip the best gear in the bags
constexpr uint32 kServiceBuyAmmoBit = 8;   // spell_id bit (layout 15): buy ammunition, up to dest_x pieces carried
constexpr uint32 kServiceBuyFoodBit = 16;  // ... food, up to dest_y
constexpr uint32 kServiceBuyDrinkBit = 32; // ... drink, up to dest_z
constexpr uint32 kServiceBuyHealthPotionBit = 64;  // layout 16: health potions, up to Bot.Supplies.HealthPotionTarget carried
constexpr uint32 kServiceBuyManaPotionBit = 128;   // ... mana potions (Bot.Supplies.ManaPotionTarget)
constexpr uint32 kServiceBuyBandageBit = 256;      // ... bandages (Bot.Supplies.BandageTarget)
constexpr uint32 kServiceBuyReagentBit = 512;      // ... the reagent the core noted a cast was missing (Bot.Supplies.ReagentTarget)
constexpr uint32 kServiceSellJunkBit = 1024;       // SDK 0.16: also sell white junk (trade goods, spare bags, miscellaneous items) with the grey items
constexpr uint32 kServiceBuyMask = kServiceBuyAmmoBit | kServiceBuyFoodBit | kServiceBuyDrinkBit | kServiceBuyHealthPotionBit |
    kServiceBuyManaPotionBit | kServiceBuyBandageBit | kServiceBuyReagentBit;
constexpr float kMaxSupplyTarget = 2000.0f; // a sanity bound on the counts a module asks for
constexpr uint32 kQualityPoor = 0;      // grey
constexpr uint32 kQualityCommon = 1;    // white: the lowest quality the bot equips

// The closest NPC in interaction range that has one of `flags` and that the bot can interact with, or nullptr.
Creature* NearestServiceNpc(Player& bot, NPCFlags flags)
{
    std::list<Unit*> inRange;
    Trinity::AnyUnitInObjectRangeCheck check(&bot, INTERACTION_DISTANCE);
    Trinity::UnitListSearcher<Trinity::AnyUnitInObjectRangeCheck> searcher(&bot, inRange, check);
    Cell::VisitAllObjects(&bot, searcher, INTERACTION_DISTANCE);

    Creature* best = nullptr;
    float bestDist = INTERACTION_DISTANCE;
    for (Unit* unit : inRange)
    {
        Creature* npc = unit->ToCreature();
        if (!npc || !npc->HasNpcFlag(flags))
            continue;
        float const dist = bot.GetDistance(npc);
        if (dist >= bestDist || !bot.GetNPCIfCanInteractWith(npc->GetGUID(), flags, UNIT_NPC_FLAG_2_NONE))
            continue;
        best = npc;
        bestDist = dist;
    }
    return best;
}

// A white item a bot has no use for, so that a full bag can be emptied: trade goods (cloth, meat, leather...), a spare bag, miscellaneous items. Never a
// quest item, one that starts a quest, a bag that is equipped (the bags in the bag slots), or anything the bot eats, drinks, shoots or casts with
// (consumables, projectiles, reagents), or wears or wields (weapons, armour): those have their own rules.
bool IsWhiteJunk(Item const& item, ItemTemplate const& proto)
{
    if (proto.GetQuality() != kQualityCommon || proto.GetBonding() == BIND_QUEST || proto.GetStartQuest() != 0)
        return false;
    if (item.GetBagSlot() == INVENTORY_SLOT_BAG_0 && item.GetSlot() >= INVENTORY_SLOT_BAG_START && item.GetSlot() < INVENTORY_SLOT_BAG_END)
        return false;
    switch (proto.GetClass())
    {
        case ITEM_CLASS_TRADE_GOODS:
        case ITEM_CLASS_CONTAINER:
        case ITEM_CLASS_MISCELLANEOUS:
            return true;
        default:
            return false;
    }
}

// The pieces of each trade good (cloth, meat, leather...) a bot keeps when it sells its white junk: cloth is for First Aid, meat for Cooking, and the rest
// can go to the auction house, so only what is above this is sold to the vendor. Whole stacks are kept until the total reaches it.
constexpr uint32 kJunkKeepTradeGoods = 60;

// Sells every grey item in the bags to the vendor, and with `whiteJunk` the white junk of IsWhiteJunk too (of the trade goods only the surplus above
// kJunkKeepTradeGoods of an entry). Returns how many stacks were sold.
uint32 SellGreyItems(Player& bot, bool whiteJunk = false)
{
    // Collected first: selling moves the items out of the bags, which the search must not be walking while it does.
    std::vector<Item*> grey;
    std::map<uint32, uint32> keepLeft; // trade goods entry -> pieces still to keep
    bot.ForEachItem(ItemSearchLocation::Inventory, [&grey, &keepLeft, whiteJunk](Item* item)
    {
        ItemTemplate const* proto = item->GetTemplate();
        if (!proto || proto->GetSellPrice() == 0)
            return ItemSearchCallbackResult::Continue;
        if (proto->GetQuality() == kQualityPoor)
        {
            grey.push_back(item);
            return ItemSearchCallbackResult::Continue;
        }
        if (!whiteJunk || !IsWhiteJunk(*item, *proto))
            return ItemSearchCallbackResult::Continue;
        if (proto->GetClass() == ITEM_CLASS_TRADE_GOODS)
        {
            uint32& left = keepLeft.try_emplace(item->GetEntry(), kJunkKeepTradeGoods).first->second;
            if (left > 0)
            {
                left = left > item->GetCount() ? left - item->GetCount() : 0;
                return ItemSearchCallbackResult::Continue; // kept: a working supply
            }
        }
        grey.push_back(item);
        return ItemSearchCallbackResult::Continue;
    });

    uint32 sold = 0;
    for (Item* item : grey)
        if (!bot.SellItemToVendor(item, item->GetCount())) // no value means it was sold
            ++sold;
    return sold;
}

constexpr uint32 kBasicCampfireSpell = 818; // Cooking: a campfire that lasts a minute, which the cooking recipes need near the cook
constexpr uint32 kCookingFireFocus = 4;     // the spell focus of a cooking fire (SpellFocusObject): a campfire or a hearth

// The skill lines kSkillUp and kCraft make things for: the two secondary ones, and the crafting professions. Enchanting is left out (its recipes work on an item
// the bot would have to name); 186 is Mining's line, whose recipes are the smelting of ore.
bool IsCraftSkill(uint32 skill)
{
    switch (skill)
    {
        case BotProfessions::kSkillFirstAid:
        case SKILL_COOKING:
        case SKILL_BLACKSMITHING:
        case SKILL_LEATHERWORKING:
        case SKILL_ALCHEMY:
        case SKILL_TAILORING:
        case SKILL_ENGINEERING:
        case SKILL_MINING:
            return true;
        default:
            return false;
    }
}

// The spell focus ids a game object of the world provides (an anvil, a forge, a fire...). A recipe that asks for any other focus can be crafted by nobody on
// this server (the recipe dump shows about 90 such recipes), so the recipe choice leaves them out. Built on first use, after the object templates are loaded.
std::set<uint32> const& FocusIdsInWorld()
{
    static std::set<uint32> const ids = []
    {
        std::set<uint32> found;
        for (auto const& [entry, templ] : sObjectMgr->GetGameObjectTemplates())
            if (templ.type == GAMEOBJECT_TYPE_SPELL_FOCUS)
                found.insert(templ.spellFocus.spellFocusType);
        return found;
    }();
    return ids;
}

// Whether a game object of the spell focus `focus` stands within reach of the bot: the search a spell with RequiresSpellFocus makes at cast time (Spell::SearchSpellFocus).
bool FocusNear(Player& bot, uint32 focus)
{
    GameObject* object = nullptr;
    Trinity::GameObjectFocusCheck check(&bot, focus);
    Trinity::GameObjectSearcher<Trinity::GameObjectFocusCheck> searcher(&bot, object, check);
    Cell::VisitGridObjects(&bot, searcher, bot.GetVisibilityRange());
    return object != nullptr;
}

bool HasReagents(Player& bot, SpellInfo const& info)
{
    for (std::size_t i = 0; i < info.Reagent.size(); ++i)
        if (info.Reagent[i] > 0 && !bot.HasItemCount(uint32(info.Reagent[i]), uint32(std::max<int32>(1, info.ReagentCount[i]))))
            return false;
    return true;
}

// A recipe the bots can cast on themselves: not one that works on an item (an enchantment, a weapon chain).
bool WorksOnSelf(SpellInfo const& info)
{
    return (info.Targets & (TARGET_FLAG_ITEM | TARGET_FLAG_TRADE_ITEM)) == 0;
}

// The recipe a bot makes to raise `skill` (First Aid, Cooking, a crafting profession): among the recipes it knows of that skill line, those that can still raise
// the skill (its value is between the recipe's skill and the point where the recipe turns grey), that it has the materials for and whose focus is no obstacle,
// the one that needs the most skill, which raises it fastest. A focus is no obstacle when the recipe needs none, an object of it stands near, or it is the
// cooking fire (a campfire is lit). A recipe that asks for a focus no object provides is never chosen. 0 and a reason when there is none; `needsFocus` is the focus
// id when recipes were left out only because their focus is not near (the answer is then kNeedsFocus: walk to one), else 0.
uint32 ChooseSkillUpRecipe(Player& bot, uint32 skill, std::string& reason, uint32& needsFocus)
{
    needsFocus = 0;
    if (!bot.HasSkill(skill))
    {
        reason = "bot does not have skill " + std::to_string(skill);
        return 0;
    }
    uint16 const value = bot.GetPureSkillValue(skill);
    uint32 best = 0;
    int32 bestRank = -1;
    bool anyRecipe = false, anyCanRaise = false, anyUnobtainable = false;
    for (auto const& [spellId, playerSpell] : bot.GetSpellMap())
    {
        if (playerSpell.state == PLAYERSPELL_REMOVED || !playerSpell.active || playerSpell.disabled)
            continue;
        auto const bounds = sSpellMgr->GetSkillLineAbilityMapBounds(spellId);
        for (auto it = bounds.first; it != bounds.second; ++it)
        {
            SkillLineAbilityEntry const* ability = it->second;
            if (ability->SkillLine != skill || ability->MinSkillLineRank <= 0)
                continue; // not a recipe of this skill (the skill's own spell has no minimum)
            anyRecipe = true;
            if (value < uint16(ability->MinSkillLineRank) || value >= uint16(ability->TrivialSkillLineRankHigh))
                continue;
            anyCanRaise = true;
            SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE);
            if (!info || !WorksOnSelf(*info) || !HasReagents(bot, *info))
                continue;
            if (uint32 const focus = info->RequiresSpellFocus; focus && focus != kCookingFireFocus)
            {
                if (!FocusIdsInWorld().count(focus))
                {
                    anyUnobtainable = true; // no object of this focus exists on this server
                    continue;
                }
                if (!FocusNear(bot, focus))
                {
                    if (!needsFocus)
                        needsFocus = focus;
                    continue;
                }
            }
            if (int32(ability->MinSkillLineRank) > bestRank)
            {
                best = spellId;
                bestRank = int32(ability->MinSkillLineRank);
            }
        }
    }
    if (!best)
    {
        if (needsFocus)
            reason = "the recipes the bot can make need a spell focus (" + std::to_string(needsFocus) + ": an anvil, a forge or a fire) within reach";
        else
            reason = !anyRecipe ? "bot knows no recipe of skill " + std::to_string(skill)
                   : !anyCanRaise ? "no recipe the bot knows can still raise its skill"
                   : anyUnobtainable ? "every recipe the bot has the materials for needs a focus object this world does not have"
                   : "bot lacks the materials of every recipe that can raise its skill";
    }
    return best;
}

// Casts a recipe on the bot: the checks and the casting that kSkillUp and kCraft share. A cooking recipe whose fire is not near makes the bot light a campfire
// (kApplied, and the next ask cooks); any other focus that is not near is kNeedsFocus.
BotActionStatus CastRecipe(Player& bot, uint32 recipe, std::string& reason)
{
    if (bot.isMoving())
    {
        SpellInfo const* recipeInfo = sSpellMgr->GetSpellInfo(recipe, DIFFICULTY_NONE);
        if (recipeInfo && recipeInfo->CalcCastTime() > 0)
        {
            reason = "recipe " + std::to_string(recipe) + " has a cast time and the bot is moving";
            return BotActionStatus::kMoving;
        }
    }
    SpellCastResult result = bot.CastSpell(&bot, recipe, false);
    if (result == SPELL_FAILED_REQUIRES_SPELL_FOCUS)
    {
        SpellInfo const* recipeInfo = sSpellMgr->GetSpellInfo(recipe, DIFFICULTY_NONE);
        if (!recipeInfo || recipeInfo->RequiresSpellFocus != kCookingFireFocus || !sSpellMgr->GetSpellInfo(kBasicCampfireSpell, DIFFICULTY_NONE))
        {
            reason = "recipe " + std::to_string(recipe) + " needs spell focus " + std::to_string(recipeInfo ? recipeInfo->RequiresSpellFocus : 0) + " within reach";
            return BotActionStatus::kNeedsFocus;
        }
        // The recipe needs a cooking fire and none is near: light one, and the next ask cooks. Any cook may light a basic campfire; the cooking
        // trainer does not list it for the bots (it was missing on 300 of them), so it is learned the first time it is needed.
        if (!bot.HasSpell(kBasicCampfireSpell))
            bot.LearnSpell(kBasicCampfireSpell, false);
        result = bot.CastSpell(&bot, kBasicCampfireSpell, false);
        if (result == SPELL_FAILED_REAGENTS)
        {
            // In Forever's data the campfire costs a piece of Simple Wood (item 4470; the recipe dump shows it). The snapshot now names it and a vendor that
            // sells it, and the bot buys it on its next supply visit like any missing reagent.
            ModuleBotSupplies::NoteMissingReagent(bot, kBasicCampfireSpell);
            reason = "the campfire needs a reagent the bot does not carry";
            return BotActionStatus::kRefused;
        }
        if (result == SPELL_CAST_OK)
        {
            reason = "lit a campfire for recipe " + std::to_string(recipe);
            return BotActionStatus::kApplied;
        }
    }
    if (result != SPELL_CAST_OK)
        reason = "cast of recipe " + std::to_string(recipe) + " returned result " + std::to_string(int32(result));
    return StatusForCast(result);
}

// Why the bot cannot start a craft or a gather now: dead, in combat, or already casting. Empty when it can.
BotActionStatus CannotCraftNow(Player& bot, std::string& reason, bool& refused)
{
    refused = true;
    if (bot.IsInCombat() || !bot.IsAlive())
    {
        reason = bot.IsAlive() ? "bot is in combat" : "bot is dead";
        return BotActionStatus::kRefused;
    }
    if (Spell const* current = bot.GetCurrentSpell(CURRENT_GENERIC_SPELL); current && current->getState() != SPELL_STATE_FINISHED)
    {
        reason = "bot is casting spell " + std::to_string(current->GetSpellInfo()->Id);
        return BotActionStatus::kBusy;
    }
    if (Spell const* channel = bot.GetCurrentSpell(CURRENT_CHANNELED_SPELL); channel && channel->getState() != SPELL_STATE_FINISHED)
    {
        reason = "bot is channeling spell " + std::to_string(channel->GetSpellInfo()->Id);
        return BotActionStatus::kBusy;
    }
    refused = false;
    return BotActionStatus::kApplied;
}

// kCraft: one named recipe, cast on the bot. The recipe must be one the bot knows, of a crafting skill line, that works on the bot itself.
BotActionStatus CraftAction(Player& bot, BotAction const& action, std::string& reason)
{
    bool refused = false;
    BotActionStatus const blocked = CannotCraftNow(bot, reason, refused);
    if (refused)
        return blocked;
    uint32 const recipe = action.spell_id;
    SpellInfo const* info = sSpellMgr->GetSpellInfo(recipe, DIFFICULTY_NONE);
    if (!info)
    {
        reason = "recipe " + std::to_string(recipe) + " does not exist";
        return BotActionStatus::kUnknownSpell;
    }
    if (!bot.HasSpell(recipe))
    {
        reason = "the bot has not learned recipe " + std::to_string(recipe);
        return BotActionStatus::kNotKnown;
    }
    bool isRecipe = false;
    auto const bounds = sSpellMgr->GetSkillLineAbilityMapBounds(recipe);
    for (auto it = bounds.first; it != bounds.second; ++it)
        if (IsCraftSkill(it->second->SkillLine) && it->second->MinSkillLineRank > 0)
            isRecipe = true;
    if (!isRecipe || !WorksOnSelf(*info))
    {
        reason = "spell " + std::to_string(recipe) + " is not a recipe the bots can craft on themselves";
        return BotActionStatus::kRefused;
    }
    if (!HasReagents(bot, *info))
    {
        ModuleBotSupplies::NoteMissingReagent(bot, recipe);
        reason = "the bot lacks the materials of recipe " + std::to_string(recipe);
        return BotActionStatus::kRefused;
    }
    if (uint32 const focus = info->RequiresSpellFocus; focus && focus != kCookingFireFocus)
    {
        if (!FocusIdsInWorld().count(focus))
        {
            reason = "recipe " + std::to_string(recipe) + " needs spell focus " + std::to_string(focus) + ", which no object of this world provides";
            return BotActionStatus::kRefused;
        }
        if (!FocusNear(bot, focus))
        {
            reason = "recipe " + std::to_string(recipe) + " needs spell focus " + std::to_string(focus) + " within reach";
            return BotActionStatus::kNeedsFocus;
        }
    }
    return CastRecipe(bot, recipe, reason);
}

// Sells `count` of the items of `entry` in the bags to the vendor, a stack at a time (0 = every one the bot carries of it). Returns how many
// were sold. An item the vendor will not take (no sell price, a bag that is not empty) is kept.
uint32 SellEntryToVendor(Player& bot, uint32 entry, uint32 count)
{
    std::vector<Item*> items;
    bot.ForEachItem(ItemSearchLocation::Inventory, [&items, entry](Item* item)
    {
        if (item->GetEntry() == entry)
            items.push_back(item);
        return ItemSearchCallbackResult::Continue;
    });

    uint32 remaining = count ? count : std::numeric_limits<uint32>::max();
    uint32 sold = 0;
    for (Item* item : items)
    {
        if (remaining == 0)
            break;
        uint32 const amount = std::min(item->GetCount(), remaining);
        if (bot.CanSellItemToVendor(item, amount))
            continue;
        if (!bot.SellItemToVendor(item, amount)) // no value means it was sold
        {
            sold += amount;
            remaining -= amount;
        }
    }
    return sold;
}

// The equipment slot an item of this inventory type goes in (the first of a pair, for rings and trinkets), or NULL_SLOT.
uint8 EquipSlotFor(InventoryType type)
{
    switch (type)
    {
        case INVTYPE_HEAD: return EQUIPMENT_SLOT_HEAD;
        case INVTYPE_NECK: return EQUIPMENT_SLOT_NECK;
        case INVTYPE_SHOULDERS: return EQUIPMENT_SLOT_SHOULDERS;
        case INVTYPE_BODY: return EQUIPMENT_SLOT_BODY;
        case INVTYPE_CHEST:
        case INVTYPE_ROBE: return EQUIPMENT_SLOT_CHEST;
        case INVTYPE_WAIST: return EQUIPMENT_SLOT_WAIST;
        case INVTYPE_LEGS: return EQUIPMENT_SLOT_LEGS;
        case INVTYPE_FEET: return EQUIPMENT_SLOT_FEET;
        case INVTYPE_WRISTS: return EQUIPMENT_SLOT_WRISTS;
        case INVTYPE_HANDS: return EQUIPMENT_SLOT_HANDS;
        case INVTYPE_FINGER: return EQUIPMENT_SLOT_FINGER1;
        case INVTYPE_TRINKET: return EQUIPMENT_SLOT_TRINKET1;
        case INVTYPE_CLOAK: return EQUIPMENT_SLOT_BACK;
        case INVTYPE_WEAPON:
        case INVTYPE_2HWEAPON:
        case INVTYPE_WEAPONMAINHAND: return EQUIPMENT_SLOT_MAINHAND;
        case INVTYPE_SHIELD:
        case INVTYPE_WEAPONOFFHAND:
        case INVTYPE_HOLDABLE: return EQUIPMENT_SLOT_OFFHAND;
        case INVTYPE_RANGED:
        case INVTYPE_RANGEDRIGHT:
        case INVTYPE_THROWN: return EQUIPMENT_SLOT_RANGED;
        default: return NULL_SLOT;
    }
}

// Equips each bag item that is a weapon or armour piece better than what the slot holds now. Returns how many were equipped.
uint32 EquipBestGear(Player& bot)
{
    std::vector<Item*> candidates;
    bot.ForEachItem(ItemSearchLocation::Inventory, [&candidates](Item* item)
    {
        ItemTemplate const* proto = item->GetTemplate();
        if (proto && proto->GetQuality() >= kQualityCommon &&
            (proto->GetClass() == ITEM_CLASS_WEAPON || proto->GetClass() == ITEM_CLASS_ARMOR) &&
            EquipSlotFor(proto->GetInventoryType()) != NULL_SLOT)
            candidates.push_back(item);
        return ItemSearchCallbackResult::Continue;
    });

    uint32 equipped = 0;
    for (Item* item : candidates)
    {
        // Checked against the slot as it is now: an earlier swap in this pass may have changed it.
        uint8 const slot = EquipSlotFor(item->GetTemplate()->GetInventoryType());
        Item* current = bot.GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (current && current->GetTemplate()->GetBaseItemLevel() >= item->GetTemplate()->GetBaseItemLevel())
            continue;

        uint16 const source = uint16(item->GetBagSlot()) << 8 | item->GetSlot();
        uint16 const target = uint16(INVENTORY_SLOT_BAG_0) << 8 | slot;
        uint16 destination = target;
        if (bot.CanEquipItem(slot, destination, item, true) != EQUIP_ERR_OK)
            continue;

        bot.SwapItem(source, target);
        if (bot.GetItemByPos(INVENTORY_SLOT_BAG_0, slot) == item)
            ++equipped;
    }
    return equipped;
}

// ---- kAutoGear (scenarios, doc 19): dress a bot for its level ----

// The gear the core may hand out: weapons and armour of common to Bot.AutoGear.MaxQuality (default 2, uncommon), with a slot, no
// quest-bound items and no unfinished-looking names (test, deprecated or placeholder items). Built once.
struct GearPoolStats
{
    uint32 WeaponsAndArmor = 0, QualityTooLow = 0, QualityOverCap = 0, QuestBound = 0, NoSlot = 0, Placeholder = 0, Kept = 0;
};

std::vector<ItemTemplate const*> const& GearCandidates()
{
    static std::vector<ItemTemplate const*> const candidates = []
    {
        std::vector<ItemTemplate const*> out;
        GearPoolStats stats;
        uint32 const maxQuality = uint32(std::max(1, sConfigMgr->GetIntDefault("Bot.AutoGear.MaxQuality", 2)));
        for (auto const& [id, proto] : sObjectMgr->GetItemTemplateStore())
        {
            if (proto.GetClass() != ITEM_CLASS_WEAPON && proto.GetClass() != ITEM_CLASS_ARMOR)
                continue;
            ++stats.WeaponsAndArmor;
            if (proto.GetQuality() < kQualityCommon)
            {
                ++stats.QualityTooLow;
                continue;
            }
            if (proto.GetQuality() > maxQuality)
            {
                ++stats.QualityOverCap;
                continue;
            }
            if (proto.GetBonding() == BIND_QUEST)
            {
                ++stats.QuestBound;
                continue;
            }
            if (proto.GetInventoryType() == INVTYPE_BODY || EquipSlotFor(proto.GetInventoryType()) == NULL_SLOT)
            {
                ++stats.NoSlot;
                continue;
            }
            std::string name = proto.GetName(DEFAULT_LOCALE) ? proto.GetName(DEFAULT_LOCALE) : "";
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            if (name.empty() || name.find("deprecated") != std::string::npos || name.find("test") == 0 ||
                name.find("unused") != std::string::npos || name.find("[ph]") != std::string::npos || name.rfind("zz", 0) == 0)
            {
                ++stats.Placeholder;
                continue;
            }
            out.push_back(&proto);
        }
        stats.Kept = uint32(out.size());
        TC_LOG_INFO("server.worldserver",
            "BotMgr: autogear pool maxquality={} weapons_armor={} quality_below_common={} quality_over_cap={} quest_bound={} no_slot={} placeholder={} candidates={}",
            maxQuality, stats.WeaponsAndArmor, stats.QualityTooLow, stats.QualityOverCap, stats.QuestBound, stats.NoSlot, stats.Placeholder,
            stats.Kept);
        return out;
    }();
    return candidates;
}

// True for armour in the body slots that is heavier than the class wears (rules in BotGearRules.h).
bool ArmorTooHeavy(Player const& bot, ItemTemplate const& item)
{
    return BotGearRules::ArmorTooHeavy(bot.GetClass(), bot.GetLevel(), item.GetClass(), item.GetSubClass(), item.GetInventoryType());
}

// What a class wants from its gear, as item-mod stats: the stats it uses, in no order. `profile` (bits 2-4 of AutoGearAction's
// flags, from MOD-Forever-Bots' combat_rotation.cpp GearProfileFor) is spec-aware when the module sent one; 0 is also the
// encoding a module build that predates this falls back to, so it still gets this class's old, class-only default.
enum class GearProfile : uint32 { StrStam = 0, AgiStam = 1, IntSpiritStam = 2, IntCrit = 3, IntSpiritHaste = 4, StrStamDef = 5, AgiStamDef = 6 };

std::vector<int32> PreferredStats(uint8 classId, GearProfile profile)
{
    switch (profile)
    {
        case GearProfile::AgiStam: return { ITEM_MOD_AGILITY, ITEM_MOD_STAMINA };
        case GearProfile::IntCrit: return { ITEM_MOD_INTELLECT, ITEM_MOD_CRIT_RATING, ITEM_MOD_STAMINA };
        case GearProfile::IntSpiritHaste: return { ITEM_MOD_INTELLECT, ITEM_MOD_SPIRIT, ITEM_MOD_HASTE_RATING, ITEM_MOD_STAMINA };
        case GearProfile::IntSpiritStam: return { ITEM_MOD_INTELLECT, ITEM_MOD_SPIRIT, ITEM_MOD_STAMINA };
        // A shield tank (protection warrior/paladin): stamina to survive, dodge/parry/defense/block to avoid and mitigate hits.
        case GearProfile::StrStamDef: return { ITEM_MOD_STRENGTH, ITEM_MOD_STAMINA, ITEM_MOD_DODGE_RATING, ITEM_MOD_PARRY_RATING,
            ITEM_MOD_DEFENSE_SKILL_RATING, ITEM_MOD_BLOCK_RATING };
        // A feral tank druid: no shield, so no block/parry gear stat to chase (bear form has no parry either).
        case GearProfile::AgiStamDef: return { ITEM_MOD_AGILITY, ITEM_MOD_STAMINA, ITEM_MOD_DODGE_RATING, ITEM_MOD_DEFENSE_SKILL_RATING };
        case GearProfile::StrStam:
        default:
            // profile 0 doubles as "no profile sent" (an old module build): fall back to the previous class-only rule.
            switch (classId)
            {
                case CLASS_WARRIOR:
                case CLASS_PALADIN: return { ITEM_MOD_STRENGTH, ITEM_MOD_STAMINA };
                case CLASS_HUNTER: return { ITEM_MOD_AGILITY, ITEM_MOD_STAMINA };
                case CLASS_ROGUE: return { ITEM_MOD_AGILITY, ITEM_MOD_STRENGTH, ITEM_MOD_STAMINA };
                default: return { ITEM_MOD_INTELLECT, ITEM_MOD_SPIRIT, ITEM_MOD_STAMINA }; // priest, shaman, mage, warlock, druid
            }
    }
}

// Higher is better: the item level first, then how well its stats suit the class (a stat the class does not use counts against it).
int32 GearScore(std::vector<int32> const& preferred, ItemTemplate const& item)
{
    int32 stats = 0;
    for (uint32 i = 0; i < MAX_ITEM_PROTO_STATS; ++i)
    {
        int32 const stat = item.GetStatModifierBonusStat(i);
        int32 const amount = item.GetStatPercentEditor(i);
        if (amount <= 0)
            continue;
        bool const wanted = std::find(preferred.begin(), preferred.end(), stat) != preferred.end();
        bool const primary = stat == ITEM_MOD_STRENGTH || stat == ITEM_MOD_AGILITY || stat == ITEM_MOD_INTELLECT || stat == ITEM_MOD_SPIRIT;
        stats += wanted ? amount : (primary ? -3 * amount : 0);
    }
    return int32(item.GetBaseItemLevel()) * 10 + stats;
}

struct GearPick
{
    ItemTemplate const* Item = nullptr;
    int32 Score = std::numeric_limits<int32>::min();
};

void Consider(GearPick& pick, ItemTemplate const& item, int32 score)
{
    if (!pick.Item || score > pick.Score)
        pick = { &item, score };
}

enum class GearResult { Equipped, KeptBetter, NoBagSpace, CannotEquip };

char const* GearResultName(GearResult result)
{
    switch (result)
    {
        case GearResult::Equipped: return "equipped";
        case GearResult::KeptBetter: return "kept_worn_item_level_not_lower";
        case GearResult::NoBagSpace: return "no_bag_space";
        case GearResult::CannotEquip: return "cannot_equip_removed";
    }
    return "unknown";
}

// Creates the item in the bags and moves it into `slot`, unless the slot already holds an item of at least this item level.
GearResult GiveAndEquip(Player& bot, ItemTemplate const& item, uint8 slot, uint32& equipError)
{
    equipError = 0;
    Item* current = bot.GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
    if (current && current->GetTemplate()->GetBaseItemLevel() >= item.GetBaseItemLevel())
        return GearResult::KeptBetter;

    ItemPosCountVec destination;
    if (bot.CanStoreNewItem(NULL_BAG, NULL_SLOT, destination, item.GetId(), 1) != EQUIP_ERR_OK)
        return GearResult::NoBagSpace;
    Item* created = bot.StoreNewItem(destination, item.GetId(), true);
    if (!created)
        return GearResult::NoBagSpace;

    uint16 target = uint16(INVENTORY_SLOT_BAG_0) << 8 | slot;
    uint16 equipDestination = target;
    InventoryResult const canEquip = bot.CanEquipItem(slot, equipDestination, created, true);
    if (canEquip != EQUIP_ERR_OK)
    {
        equipError = uint32(canEquip);
        // Not left in the bags: the same pick comes back at every gear pass and would fill the bags with copies.
        bot.DestroyItem(created->GetBagSlot(), created->GetSlot(), true);
        return GearResult::CannotEquip;
    }
    bot.SwapItem(uint16(created->GetBagSlot()) << 8 | created->GetSlot(), target);
    return bot.GetItemByPos(INVENTORY_SLOT_BAG_0, slot) == created ? GearResult::Equipped : GearResult::CannotEquip;
}

// spell_id bit 1: a tank (one-hand weapon and shield).
// Gear the bot was given or looted: an item in the bags that the bot can use and that has a higher item level than what is worn where it
// would go is worn instead (the displaced item goes to the bags). The quality cap of the item pool does not apply: a rare that a player
// trades to a bot is worn. The comparison is the item level, the same rule GiveAndEquip keeps a worn item by, so the two passes cannot
// undo each other.
uint32 EquipBagUpgrades(Player& bot)
{
    std::vector<Item*> candidates;
    bot.ForEachItem(ItemSearchLocation::Inventory, [&candidates](Item* item)
    {
        ItemTemplate const* proto = item->GetTemplate();
        if (proto && (proto->GetClass() == ITEM_CLASS_WEAPON || proto->GetClass() == ITEM_CLASS_ARMOR) &&
            proto->GetInventoryType() != INVTYPE_BODY && EquipSlotFor(proto->GetInventoryType()) != NULL_SLOT)
            candidates.push_back(item);
        return ItemSearchCallbackResult::Continue;
    });
    // the best item level first
    std::stable_sort(candidates.begin(), candidates.end(), [](Item const* a, Item const* b)
    {
        return a->GetTemplate()->GetBaseItemLevel() > b->GetTemplate()->GetBaseItemLevel();
    });

    uint32 equipped = 0;
    for (Item* item : candidates)
    {
        ItemTemplate const* proto = item->GetTemplate();
        if (proto->GetBaseRequiredLevel() > int32(bot.GetLevel()) || bot.CanUseItem(proto) != EQUIP_ERR_OK || ArmorTooHeavy(bot, *proto))
            continue;
        uint16 destination = 0;
        if (bot.CanEquipItem(NULL_SLOT, destination, item, true) != EQUIP_ERR_OK)
            continue;
        uint8 const slot = uint8(destination & 255);
        Item* worn = bot.GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (worn && worn->GetTemplate()->GetBaseItemLevel() >= proto->GetBaseItemLevel())
            continue;
        bot.SwapItem(item->GetPos(), destination);
        if (bot.GetItemByPos(INVENTORY_SLOT_BAG_0, slot) == item)
        {
            ++equipped;
            TC_LOG_INFO("server.worldserver", "BotMgr: autogear bag upgrade bot={} slot={} item={} ilvl={}", bot.GetName(), uint32(slot), proto->GetId(),
                proto->GetBaseItemLevel());
        }
    }
    return equipped;
}

int32 BotGearUpgradeScore(Player& bot, ItemTemplate const& item)
{
    if (item.GetClass() != ITEM_CLASS_WEAPON && item.GetClass() != ITEM_CLASS_ARMOR)
        return 0;
    InventoryType const type = item.GetInventoryType();
    uint8 const first = EquipSlotFor(type);
    if (type == INVTYPE_BODY || first == NULL_SLOT)
        return 0;
    if (item.GetBaseRequiredLevel() > int32(bot.GetLevel()) || bot.CanUseItem(&item) != EQUIP_ERR_OK || ArmorTooHeavy(bot, item))
        return 0;
    // A two-hander would also take the off hand out of use: no judgement, no purchase.
    if (type == INVTYPE_2HWEAPON && bot.GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND))
        return 0;

    // Where the item goes: one slot, or the weaker of the two for rings and trinkets (an empty one first).
    Item* worn = bot.GetItemByPos(INVENTORY_SLOT_BAG_0, first);
    if ((type == INVTYPE_FINGER || type == INVTYPE_TRINKET) && worn)
    {
        Item* second = bot.GetItemByPos(INVENTORY_SLOT_BAG_0, uint8(first + 1));
        if (!second || second->GetTemplate()->GetBaseItemLevel() < worn->GetTemplate()->GetBaseItemLevel())
            worn = second;
    }
    if (worn && worn->GetTemplate()->GetBaseItemLevel() >= item.GetBaseItemLevel())
        return 0;

    std::vector<int32> const preferred = PreferredStats(bot.GetClass(), GearProfile::StrStam);
    int32 const gain = GearScore(preferred, item) - (worn ? GearScore(preferred, *worn->GetTemplate()) : 0);
    return gain > 0 ? gain : 0;
}

BotActionStatus AutoGearAction(Player& bot, uint32 flags, std::string& reason)
{
    // spell_id bit 2: only wear what is better in the bags (EquipBagUpgrades), no pass over the item pool. The module asks for this on its own
    // schedule and after a trade, so a bot wears what it was given or looted whether or not an auto gear pass is due.
    bool const bagOnly = (flags & 2) != 0;
    static bool const allowed = sConfigMgr->GetBoolDefault("Bot.Actions.AutoGear", true);
    static bool const upgradesAllowed = sConfigMgr->GetBoolDefault("Bot.Actions.EquipUpgrades", true);
    if (bagOnly ? !upgradesAllowed : !allowed)
    {
        reason = bagOnly ? "Bot.Actions.EquipUpgrades is off" : "Bot.Actions.AutoGear is off";
        return BotActionStatus::kRefused;
    }
    if (bot.IsInCombat())
    {
        reason = "bot is in combat";
        return BotActionStatus::kRefused;
    }
    if (bot.IsCharmed())
    {
        reason = "bot is charmed";
        return BotActionStatus::kRefused;
    }
    if (sBotMgr->IsActiveAlt(bot.GetGUID().GetCounter()))
    {
        reason = "an alt is a player's own character and is not dressed";
        return BotActionStatus::kRefused;
    }
    if (bagOnly)
    {
        EquipBagUpgrades(bot);
        return BotActionStatus::kApplied;
    }

    // Player::CanEquipItem refuses every item (EQUIP_ERR_CLIENT_LOCKED_OUT) while a non-instant generic or channeled spell is
    // mid-cast, the same guard the real client's UI enforces. A bot has no client round-trip to clear this after an interrupted
    // or aborted cast, so without this a bot could get stuck failing every single gear pick, forever, with no further sign of
    // why beyond "equip_error=40" in the log (2026-10-28: this is exactly what was seen -- every pick for one bot failed this
    // way). IsInCombat() above does not cover it: a cast can outlast combat ending, or start and abort outside combat entirely.
    bot.InterruptNonMeleeSpells(true);

    bool const tank = (flags & 1) != 0;
    std::vector<int32> const preferred = PreferredStats(bot.GetClass(), GearProfile((flags >> 2) & 7));
    std::map<uint8, GearPick> bySlot;          // armour and ranged, by equipment slot
    GearPick twoHand, shield, holdable;
    std::vector<std::pair<int32, ItemTemplate const*>> oneHand;   // main-hand weapons (one-hand, main hand)
    std::vector<std::pair<int32, ItemTemplate const*>> offHand;    // off-hand weapons (off hand only, and one-hand for dual wield)

    uint32 const candidates = uint32(GearCandidates().size());
    uint32 levelTooHigh = 0, cannotUse = 0;
    for (ItemTemplate const* item : GearCandidates())
    {
        if (item->GetBaseRequiredLevel() > int32(bot.GetLevel()))
        {
            ++levelTooHigh;
            continue;
        }
        if (bot.CanUseItem(item) != EQUIP_ERR_OK || ArmorTooHeavy(bot, *item))
        {
            ++cannotUse;
            continue;
        }
        int32 const score = GearScore(preferred, *item);
        switch (item->GetInventoryType())
        {
            case INVTYPE_WEAPON:
                oneHand.emplace_back(score, item);
                offHand.emplace_back(score, item);
                break;
            case INVTYPE_WEAPONMAINHAND: oneHand.emplace_back(score, item); break;
            case INVTYPE_WEAPONOFFHAND: offHand.emplace_back(score, item); break;
            case INVTYPE_2HWEAPON: Consider(twoHand, *item, score); break;
            case INVTYPE_SHIELD: Consider(shield, *item, score); break;
            case INVTYPE_HOLDABLE: Consider(holdable, *item, score); break;
            default: Consider(bySlot[EquipSlotFor(item->GetInventoryType())], *item, score); break;
        }
    }

    auto const best = [](std::vector<std::pair<int32, ItemTemplate const*>> const& list, ItemTemplate const* not_this) -> GearPick
    {
        GearPick pick;
        for (auto const& [score, item] : list)
            if (item != not_this)
                Consider(pick, *item, score);
        return pick;
    };

    // The weapons: a tank takes one hand and a shield; a dual-wielder two one-handers; otherwise a two-hander when it is at least as
    // good as the best one-hander, else one hand and a shield or a held item.
    GearPick main, off;
    GearPick const mainOne = best(oneHand, nullptr);
    if (tank && mainOne.Item && shield.Item)
    {
        main = mainOne;
        off = shield;
    }
    else if (bot.CanDualWield() && mainOne.Item && best(offHand, mainOne.Item).Item)
    {
        main = mainOne;
        off = best(offHand, mainOne.Item);
    }
    else if (twoHand.Item && (!mainOne.Item || twoHand.Score >= mainOne.Score))
        main = twoHand;
    else
    {
        main = mainOne;
        off = shield.Item ? shield : holdable;
    }

    uint32 equipped = 0, keptBetter = 0, failed = 0, tried = 0;
    auto const apply = [&](GearPick const& pick, uint8 slot)
    {
        ++tried;
        uint32 equipError = 0;
        GearResult const result = GiveAndEquip(bot, *pick.Item, slot, equipError);
        equipped += result == GearResult::Equipped;
        keptBetter += result == GearResult::KeptBetter;
        failed += result == GearResult::NoBagSpace || result == GearResult::CannotEquip;
        TC_LOG_INFO("server.worldserver", "BotMgr: autogear pick bot={} slot={} item={} quality={} ilvl={} reqlevel={} armor_subclass={} result={} equip_error={}", bot.GetName(), uint32(slot),
            pick.Item->GetId(), uint32(pick.Item->GetQuality()), pick.Item->GetBaseItemLevel(), pick.Item->GetBaseRequiredLevel(),
            pick.Item->GetClass() == ITEM_CLASS_ARMOR ? int32(pick.Item->GetSubClass()) : -1, GearResultName(result), equipError);
    };
    if (main.Item)
        apply(main, EQUIPMENT_SLOT_MAINHAND);
    if (off.Item && (!main.Item || main.Item->GetInventoryType() != INVTYPE_2HWEAPON))
        apply(off, EQUIPMENT_SLOT_OFFHAND);
    for (auto const& [slot, pick] : bySlot)
        if (pick.Item)
            apply(pick, slot);

    // Gear the bot already carries (a trade, a loot) is worn when it beats what is worn.
    equipped += EquipBagUpgrades(bot);

    // What the bot wears now, from the loaded item template store, each against the quality cap.
    uint32 const maxQuality = uint32(std::max(1, sConfigMgr->GetIntDefault("Bot.AutoGear.MaxQuality", 2)));
    uint32 wornItems = 0, overCap = 0;
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        Item* worn = bot.GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!worn)
            continue;
        ItemTemplate const* proto = worn->GetTemplate();
        ++wornItems;
        bool const withinCap = proto->GetQuality() <= maxQuality;
        overCap += !withinCap;
        TC_LOG_INFO("server.worldserver", "BotMgr: gear bot={} slot={} item={} quality={} ilvl={} reqlevel={} within_cap={}", bot.GetName(), uint32(slot),
            proto->GetId(), uint32(proto->GetQuality()), proto->GetBaseItemLevel(), proto->GetBaseRequiredLevel(), withinCap ? 1 : 0);
    }

    TC_LOG_INFO("server.worldserver",
        "BotMgr: autogear {} (class {}, level {}): {} item(s) equipped. summary bot={} class={} level={} considered={} level_too_high={} cannot_use={} picks={} equipped={} kept_better={} failed={} worn={} over_cap={}",
        bot.GetName(), uint32(bot.GetClass()), uint32(bot.GetLevel()), equipped, bot.GetName(), uint32(bot.GetClass()), uint32(bot.GetLevel()), candidates,
        levelTooHigh, cannotUse, tried, equipped, keptBetter, failed, wornItems, overCap);
    return BotActionStatus::kApplied;
}

// Learns every spell the trainer in range teaches this bot now: lowest level first, so each rank comes before the next. The
// core's own teach checks decide money, class and prerequisites, so nothing here is granted that the trainer would refuse.
// The closest creature in interaction range that starts (`ender` false) or ends the quest, or nullptr.
Creature* NearestQuestNpc(Player& bot, uint32 questId, bool ender)
{
    std::list<Unit*> inRange;
    Trinity::AnyUnitInObjectRangeCheck check(&bot, INTERACTION_DISTANCE);
    Trinity::UnitListSearcher<Trinity::AnyUnitInObjectRangeCheck> searcher(&bot, inRange, check);
    Cell::VisitAllObjects(&bot, searcher, INTERACTION_DISTANCE);

    Creature* best = nullptr;
    float bestDist = INTERACTION_DISTANCE;
    for (Unit* unit : inRange)
    {
        Creature* npc = unit->ToCreature();
        if (!npc || !(ender ? npc->hasInvolvedQuest(questId) : npc->hasQuest(questId)))
            continue;
        float const dist = bot.GetDistance(npc);
        if (dist >= bestDist || !bot.CanInteractWithQuestGiver(npc))
            continue;
        best = npc;
        bestDist = dist;
    }
    return best;
}

// The reward a bot picks from a quest's choices: the item it can use with the highest sell price, else the highest sell price. 0 when
// the quest offers no choice.
uint32 PickQuestReward(Player& bot, Quest const& quest)
{
    uint32 best = 0;
    bool bestUsable = false;
    uint64 bestPrice = 0;
    for (uint32 i = 0; i < quest.GetRewChoiceItemsCount(); ++i)
    {
        if (!quest.RewardChoiceItemId[i] || quest.RewardChoiceItemType[i] != LootItemType::Item)
            continue;
        ItemTemplate const* item = sObjectMgr->GetItemTemplate(quest.RewardChoiceItemId[i]);
        if (!item)
            continue;
        bool const usable = bot.CanUseItem(item) == EQUIP_ERR_OK;
        uint64 const price = item->GetSellPrice();
        if (!best || (usable && !bestUsable) || (usable == bestUsable && price > bestPrice))
        {
            best = item->GetId();
            bestUsable = usable;
            bestPrice = price;
        }
    }
    return best;
}

// kQuest: spell_id = (operation << 24) | quest id. 1 accepts the quest from a giver in range, 2 hands it in at an ender in range
// (picking the reward), 3 abandons it, 4 completes it with its reward and no objectives, anywhere (what the GM command
// `.quest reward` does; the planner sends it for class quests).
BotActionStatus QuestAction(Player& bot, uint32 code, std::string& reason)
{
    uint32 const operation = code >> 24;
    uint32 const questId = code & 0xFFFFFF;
    Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
    if (!quest)
    {
        reason = "unknown quest";
        return BotActionStatus::kRefused;
    }

    if (operation == 3)
    {
        if (bot.GetQuestStatus(questId) == QUEST_STATUS_NONE)
        {
            reason = "quest is not in the log";
            return BotActionStatus::kRefused;
        }
        bot.RemoveActiveQuest(questId);
        return BotActionStatus::kApplied;
    }

    if (operation == 4)
    {
        if (bot.GetQuestStatus(questId) == QUEST_STATUS_NONE)
        {
            reason = "quest is not in the log";
            return BotActionStatus::kRefused;
        }
        uint32 const reward = PickQuestReward(bot, *quest);
        bot.CompleteQuest(questId);
        if (!bot.CanRewardQuest(quest, LootItemType::Item, reward, false))
        {
            reason = "bot cannot receive the reward (bags full)";
            return BotActionStatus::kRefused;
        }
        bot.RewardQuest(quest, LootItemType::Item, reward, &bot);
        return BotActionStatus::kApplied;
    }

    if (operation != 1 && operation != 2)
    {
        reason = "unknown quest operation";
        return BotActionStatus::kRefused;
    }
    if (bot.IsInCombat())
    {
        reason = "bot is in combat";
        return BotActionStatus::kRefused;
    }

    Creature* npc = NearestQuestNpc(bot, questId, operation == 2);
    if (!npc)
    {
        reason = operation == 1 ? "no giver of the quest within interaction range" : "no ender of the quest within interaction range";
        return BotActionStatus::kRefused;
    }

    if (operation == 1)
    {
        if (!bot.CanTakeQuest(quest, false))
        {
            reason = "bot cannot take the quest";
            return BotActionStatus::kRefused;
        }
        if (!bot.CanAddQuest(quest, false))
        {
            reason = "bot cannot add the quest (log or bags full)";
            return BotActionStatus::kRefused;
        }
        bot.AddQuestAndCheckCompletion(quest, npc);
        return BotActionStatus::kApplied;
    }

    if (bot.GetQuestStatus(questId) != QUEST_STATUS_COMPLETE || !bot.CanRewardQuest(quest, false))
    {
        reason = "quest is not complete";
        return BotActionStatus::kRefused;
    }
    uint32 const reward = PickQuestReward(bot, *quest);
    if (!bot.CanRewardQuest(quest, LootItemType::Item, reward, false))
    {
        reason = "bot cannot receive the reward (bags full)";
        return BotActionStatus::kRefused;
    }
    bot.RewardQuest(quest, LootItemType::Item, reward, npc);
    return BotActionStatus::kApplied;
}

// kTeleport (scenarios): spell_id is the map, dest the position. Off unless Bot.Actions.Teleport is on; refused in combat.
BotActionStatus TeleportAction(Player& bot, BotAction const& action, std::string& reason)
{
    static bool const allowed = sConfigMgr->GetBoolDefault("Bot.Actions.Teleport", false);
    if (!allowed)
    {
        reason = "Bot.Actions.Teleport is off";
        return BotActionStatus::kRefused;
    }
    if (bot.IsInCombat())
    {
        reason = "bot is in combat";
        return BotActionStatus::kRefused;
    }
    uint32 const mapId = action.spell_id;
    if (!MapManager::IsValidMapCoord(mapId, action.dest_x, action.dest_y, action.dest_z))
    {
        reason = "not a valid map position";
        return BotActionStatus::kRefused;
    }
    if (!bot.TeleportTo(mapId, action.dest_x, action.dest_y, action.dest_z, bot.GetOrientation()))
    {
        reason = "the teleport was refused (instance full, locked or not allowed)";
        return BotActionStatus::kRefused;
    }
    return BotActionStatus::kApplied;
}

// kGrant (scenarios): spell_id is an item entry, dest_x the count. Off unless Bot.Actions.Grant is on.
BotActionStatus GrantAction(Player& bot, BotAction const& action, std::string& reason)
{
    static bool const allowed = sConfigMgr->GetBoolDefault("Bot.Actions.Grant", false);
    if (!allowed)
    {
        reason = "Bot.Actions.Grant is off";
        return BotActionStatus::kRefused;
    }
    uint32 const entry = action.spell_id;
    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(entry);
    if (!proto)
    {
        reason = "unknown item";
        return BotActionStatus::kRefused;
    }
    if (!std::isfinite(action.dest_x) || action.dest_x < 1.0f || action.dest_x > float(proto->GetMaxStackSize()))
    {
        reason = "grant count must be from 1 to the item's stack size";
        return BotActionStatus::kRefused;
    }
    uint32 const count = uint32(action.dest_x);
    ItemPosCountVec destination;
    if (bot.CanStoreNewItem(NULL_BAG, NULL_SLOT, destination, entry, count) != EQUIP_ERR_OK)
    {
        reason = "the bot's bags cannot hold the item";
        return BotActionStatus::kRefused;
    }
    if (!bot.StoreNewItem(destination, entry, true))
    {
        reason = "the item could not be created";
        return BotActionStatus::kRefused;
    }
    return BotActionStatus::kApplied;
}

BotActionStatus LearnFromTrainer(Player& bot, std::string& reason)
{
    Creature* trainer = NearestServiceNpc(bot, UNIT_NPC_FLAG_TRAINER);
    if (!trainer)
    {
        reason = "no trainer within interaction range";
        return BotActionStatus::kRefused;
    }

    Trainer::Trainer const* teaches = sObjectMgr->GetTrainer(trainer->GetTrainerId());
    if (!teaches)
    {
        reason = "trainer teaches nothing";
        return BotActionStatus::kRefused;
    }

    // A bot has two primary professions (BotProfessions.h) and learns no others, whatever trainer it stands at. Its own come for free when it levels.
    if (!sBotMgr->IsActiveAlt(bot.GetGUID().GetCounter()) && !BotProfessions::TrainerMayTeach(*teaches, bot.GetGUID().GetCounter()))
    {
        reason = "the trainer teaches a profession that is not one of this bot's";
        return BotActionStatus::kRefused;
    }

    std::vector<Trainer::Spell> spells = teaches->Spells();
    std::stable_sort(spells.begin(), spells.end(), [](Trainer::Spell const& a, Trainer::Spell const& b)
    {
        return a.ReqLevel != b.ReqLevel ? a.ReqLevel < b.ReqLevel : a.SpellId < b.SpellId;
    });

    for (Trainer::Spell const& spell : spells)
        if (spell.ReqLevel <= bot.GetLevel() && !bot.HasSpell(spell.SpellId))
            teaches->TeachSpell(trainer, &bot, spell.SpellId);
    return BotActionStatus::kApplied;
}

// Party actions (M8). A bot invites only other bots the module controls, so the module's parties are its own; an invite
// never reaches a player on a real account. The steps follow the core's own invite and accept handlers (GroupHandler.cpp).
BotActionStatus InviteToParty(Player& bot, uint64 targetRef, std::string& reason, uint32 module)
{
    if (forever_module::UnitRefType(targetRef) != uint8(HighGuid::Player))
    {
        reason = "party invite target is not a player";
        return BotActionStatus::kInvalidTarget;
    }
    uint32 const counter = forever_module::UnitRefCounter(targetRef);
    Player* target = ObjectAccessor::FindPlayerByLowGUID(counter);
    if (!target || target == &bot || !target->IsInWorld() || !sBotMgr->Exists(counter) || !sModuleMgr->ControlsBot(counter, module))
    {
        reason = "party invite target is not a bot this module controls";
        return BotActionStatus::kInvalidTarget;
    }
    if (target->GetGroup() || target->GetGroupInvite())
    {
        reason = "party invite target is already in a party or invited";
        return BotActionStatus::kRefused;
    }
    if (!sWorld->getBoolConfig(CONFIG_ALLOW_TWO_SIDE_INTERACTION_GROUP) && bot.GetTeam() != target->GetTeam())
    {
        reason = "party invite target is on the other team";
        return BotActionStatus::kRefused;
    }

    // A bot already in a party, or leading a pending one, invites into it. Only its leader does.
    Group* group = bot.GetGroup();
    if (!group)
        group = bot.GetGroupInvite();
    if (group && !group->IsLeader(bot.GetGUID()) && !group->IsAssistant(bot.GetGUID()))
    {
        reason = "bot is not the party leader";
        return BotActionStatus::kRefused;
    }
    if (group && group->IsFull())
    {
        reason = "party is full";
        return BotActionStatus::kRefused;
    }

    bool const fresh = group == nullptr;
    if (fresh)
    {
        // The party is not saved until a second member joins, so a failed invite just drops it.
        group = new Group();
        if (!group->AddLeaderInvite(&bot))
        {
            delete group;
            reason = "could not start a party";
            return BotActionStatus::kRefused;
        }
    }
    if (!group->AddInvite(target))
    {
        if (fresh)
        {
            group->RemoveAllInvites();
            delete group;
        }
        reason = "could not invite the bot";
        return BotActionStatus::kRefused;
    }
    return BotActionStatus::kApplied;
}

// Accepts the bot's pending party invite. The first accept forms the party, as the core's accept handler does.
BotActionStatus AcceptPartyInvite(Player& bot, std::string& reason)
{
    Group* group = bot.GetGroupInvite();
    if (!group)
    {
        reason = "bot has no pending party invite";
        return BotActionStatus::kRefused;
    }

    group->RemoveInvite(&bot);
    if (group->GetLeaderGUID() == bot.GetGUID())
    {
        reason = "bot leads the party it was invited to";
        return BotActionStatus::kRefused;
    }
    if (group->IsFull())
    {
        reason = "party is full";
        return BotActionStatus::kRefused;
    }

    if (!group->IsCreated())
    {
        // The leader may be zoning; the invite is dropped rather than left dangling.
        Player* leader = ObjectAccessor::FindPlayer(group->GetLeaderGUID());
        if (!leader)
        {
            group->RemoveAllInvites();
            reason = "party leader is offline";
            return BotActionStatus::kRefused;
        }
        group->RemoveInvite(leader);
        group->Create(leader);
        sGroupMgr->AddGroup(group);
    }
    if (!group->AddMember(&bot))
    {
        reason = "could not join the party";
        return BotActionStatus::kRefused;
    }
    group->BroadcastGroupUpdate();
    return BotActionStatus::kApplied;
}

// Leaves the bot's party, or declines its pending invite when it is in none (the core's own uninvite path).
BotActionStatus LeaveParty(Player& bot, std::string& reason)
{
    if (bot.GetGroup())
    {
        bot.RemoveFromGroup(GROUP_REMOVEMETHOD_LEAVE);
        return BotActionStatus::kApplied;
    }
    if (bot.GetGroupInvite())
    {
        bot.UninviteFromGroup();
        return BotActionStatus::kApplied;
    }
    reason = "bot is not in a party";
    return BotActionStatus::kRefused;
}

// 40-man raid support (doc 108): converts the bot's own party to a raid (Group::ConvertToRaid, already fully
// implemented -- GROUP_FLAG_RAID, the 8 subgroup counters, persistence, the client notice). Only the leader or an
// assistant may do it, same as an invite. Idempotent: asking again once it is already a raid is not an error, since
// the module polls membership each tick and may see its own earlier success a tick late.
BotActionStatus ConvertGroupToRaid(Player& bot, std::string& reason)
{
    Group* group = bot.GetGroup();
    if (!group)
    {
        reason = "bot is not in a party";
        return BotActionStatus::kRefused;
    }
    if (!group->IsLeader(bot.GetGUID()) && !group->IsAssistant(bot.GetGUID()))
    {
        reason = "bot is not the party leader";
        return BotActionStatus::kRefused;
    }
    if (group->isRaidGroup())
        return BotActionStatus::kApplied;
    group->ConvertToRaid();
    return BotActionStatus::kApplied;
}

// 40-man raid support (doc 108): moves the player named by targetRef into the given subgroup (0-7) of the bot's own
// raid, via Group::ChangeMembersGroup -- the same engine call the client's raid-roster drag does. Only the raid
// leader or an assistant may reassign a member, and only within a group that is already a raid (a 5-man party has
// one subgroup and nothing to assign).
BotActionStatus AssignSubgroup(Player& bot, uint64 targetRef, uint8 subgroup, std::string& reason)
{
    if (subgroup >= MAX_RAID_SUBGROUPS)
    {
        reason = "subgroup out of range";
        return BotActionStatus::kInvalidTarget;
    }
    Group* group = bot.GetGroup();
    if (!group || !group->isRaidGroup())
    {
        reason = "bot is not in a raid group";
        return BotActionStatus::kRefused;
    }
    if (!group->IsLeader(bot.GetGUID()) && !group->IsAssistant(bot.GetGUID()))
    {
        reason = "bot is not the raid leader";
        return BotActionStatus::kRefused;
    }
    if (forever_module::UnitRefType(targetRef) != uint8(HighGuid::Player))
    {
        reason = "subgroup assign target is not a player";
        return BotActionStatus::kInvalidTarget;
    }
    uint32 const counter = forever_module::UnitRefCounter(targetRef);
    Player* target = ObjectAccessor::FindPlayerByLowGUID(counter);
    if (!target || !group->IsMember(target->GetGUID()))
    {
        reason = "subgroup assign target is not in the bot's raid";
        return BotActionStatus::kInvalidTarget;
    }
    // The policy retries a subgroup assignment until it observes the result on a later snapshot. Classify the existing
    // membership before capacity: an already-correct assignment must succeed even when its subgroup is full.
    switch (ModuleGroupRules::ClassifySubgroupMove(group->GetMemberGroup(target->GetGUID()), subgroup, group->HasFreeSlotSubGroup(subgroup)))
    {
        case ModuleGroupRules::SubgroupMove::AlreadyAssigned:
            return BotActionStatus::kApplied;
        case ModuleGroupRules::SubgroupMove::Full:
            reason = "subgroup is full";
            return BotActionStatus::kRefused;
        case ModuleGroupRules::SubgroupMove::Move:
            break;
    }
    group->ChangeMembersGroup(target->GetGUID(), subgroup);
    return BotActionStatus::kApplied;
}

// Loots a corpse the way the client does: through the bot's own session handlers (open the loot, take each item, take the money,
// release it), so the loot rights, the group rules and the bag space are the core's, not a copy of them here. The distance is the one
// the loot handler uses for a corpse. A corpse that is empty, or whose items do not all fit, is not an error: the bot took what it could.
constexpr float kLootDistanceYards = 30.0f;

// Takes every item of every loot in the bot's loot view, then the money, and releases each loot (the part of LootCorpse after the loot is open; a gathered
// node or a skinned corpse leaves its loot in the view the same way).
void TakeLootView(Player& bot, WorldSession& session)
{
    // Take every item of every loot in the view, then the money.
    WorldPackets::Loot::LootItem take{ WorldPacket(CMSG_LOOT_ITEM) };
    for (std::pair<ObjectGuid const, Loot*> const& view : bot.GetAELootView())
        for (::LootItem const& item : view.second->items)
            if (take.Loot.size() < 100)
                take.Loot.push_back({ view.first, uint8(item.LootListId) });
    if (!take.Loot.empty())
        session.HandleAutostoreLootItemOpcode(take);

    WorldPackets::Loot::LootMoney money{ WorldPacket(CMSG_LOOT_MONEY) };
    session.HandleLootMoneyOpcode(money);

    // Release each loot. The view changes as they are released, so the list is copied first.
    std::vector<Loot*> opened;
    for (std::pair<ObjectGuid const, Loot*> const& view : bot.GetAELootView())
        opened.push_back(view.second);
    for (Loot* loot : opened)
        session.DoLootRelease(loot);
}

BotActionStatus LootCorpse(Player& bot, uint64 targetRef, std::string& reason)
{
    Unit* unit = ResolveUnit(bot, targetRef);
    Creature* corpse = unit ? unit->ToCreature() : nullptr;
    if (!corpse)
    {
        reason = "loot target is not a creature on this bot's map";
        return BotActionStatus::kInvalidTarget;
    }
    if (corpse->IsAlive())
    {
        reason = "loot target is alive";
        return BotActionStatus::kInvalidTarget;
    }
    if (!bot.IsAlive())
    {
        reason = "bot is dead";
        return BotActionStatus::kRefused;
    }
    if (!bot.IsWithinDist(corpse, kLootDistanceYards))
    {
        reason = "corpse is more than 30 yards away";
        return BotActionStatus::kOutOfRange;
    }
    if (!bot.isAllowedToLoot(corpse))
    {
        reason = "bot has no right to loot this corpse";
        return BotActionStatus::kRefused;
    }

    WorldSession* session = bot.GetSession();
    if (!session)
    {
        reason = "bot has no session";
        return BotActionStatus::kRefused;
    }

    // Open the loot. Every corpse that is lootable within range joins the bot's loot view, as for a player.
    WorldPackets::Loot::LootUnit open{ WorldPacket(CMSG_LOOT_UNIT) };
    open.Unit = corpse->GetGUID();
    session->HandleLootOpcode(open);
    if (bot.GetAELootView().empty())
        return BotActionStatus::kApplied; // nothing to take

    TakeLootView(bot, *session);
    return BotActionStatus::kApplied;
}

// Gathering (kGather): what a node of Mining or Herbalism needs and what a corpse needs for Skinning. A node is a chest-type game object whose lock asks for the
// skill (Lock.dbc, LOCK_KEY_SKILL with LOCKTYPE_MINING or LOCKTYPE_HERBALISM), at the skill value the lock names.
constexpr float kGatherSearchYards = 30.0f;
constexpr float kGatherReachYards = 4.0f;
constexpr uint32 kMiningSpell = 2575, kHerbGatheringSpell = 2366, kSkinningSpell = 8613; // rank 1 of each; HighestKnownRank finds the rank the bot has

std::map<uint32, uint32> const& GatherNodes(int32 lockType) // game object entry -> the skill value its lock asks for
{
    static std::map<int32, std::map<uint32, uint32>> const byLock = []
    {
        std::map<int32, std::map<uint32, uint32>> out;
        for (auto const& [entry, templ] : sObjectMgr->GetGameObjectTemplates())
        {
            if (templ.type != GAMEOBJECT_TYPE_CHEST)
                continue;
            LockEntry const* lock = sLockStore.LookupEntry(templ.GetLockId());
            if (!lock)
                continue;
            for (std::size_t i = 0; i < lock->Type.size(); ++i)
                if (lock->Type[i] == LOCK_KEY_SKILL && (lock->Index[i] == LOCKTYPE_MINING || lock->Index[i] == LOCKTYPE_HERBALISM))
                    out[lock->Index[i]][entry] = lock->Skill[i];
        }
        return out;
    }();
    static std::map<uint32, uint32> const none;
    auto const it = byLock.find(lockType);
    return it != byLock.end() ? it->second : none;
}

struct GatherNodeCheck
{
    WorldObject const* _bot;
    std::map<uint32, uint32> const& _nodes;
    uint32 _skillValue;
    bool operator()(GameObject* object) const
    {
        auto const it = _nodes.find(object->GetEntry());
        return it != _nodes.end() && it->second <= _skillValue && object->isSpawned() && object->GetGoState() == GO_STATE_READY && _bot->IsWithinDist(object, kGatherSearchYards);
    }
};

struct SkinnableCorpseCheck
{
    WorldObject const* _bot;
    bool operator()(Creature* creature) const
    {
        return !creature->IsAlive() && creature->HasUnitFlag(UNIT_FLAG_SKINNABLE) && _bot->IsWithinDist(creature, kGatherSearchYards);
    }
};

BotActionStatus GatherAction(Player& bot, BotAction const& action, std::string& reason)
{
    uint32 const skill = action.spell_id;
    if (skill != SKILL_MINING && skill != SKILL_HERBALISM && skill != SKILL_SKINNING)
    {
        reason = "skill " + std::to_string(skill) + " is not one the bots gather with (186 Mining, 182 Herbalism, 393 Skinning)";
        return BotActionStatus::kRefused;
    }
    bool refused = false;
    BotActionStatus const blocked = CannotCraftNow(bot, reason, refused);
    if (refused)
        return blocked;
    if (!bot.HasSkill(skill))
    {
        reason = "bot does not have skill " + std::to_string(skill);
        return BotActionStatus::kRefused;
    }
    // What an earlier gather left in the loot view (a vein opened, a hide skinned) is taken first, as a kLoot takes a corpse's.
    if (!bot.GetAELootView().empty())
    {
        if (WorldSession* session = bot.GetSession())
        {
            TakeLootView(bot, *session);
            reason = "took the loot of the last gather";
            return BotActionStatus::kApplied;
        }
    }
    uint32 const baseSpell = skill == SKILL_MINING ? kMiningSpell : skill == SKILL_HERBALISM ? kHerbGatheringSpell : kSkinningSpell;
    uint32 const spell = HighestKnownRank(bot, baseSpell);
    if (!spell)
    {
        reason = "the bot has not learned the gathering spell of skill " + std::to_string(skill);
        return BotActionStatus::kNotKnown;
    }

    WorldObject* target = nullptr;
    if (skill == SKILL_SKINNING)
    {
        Creature* corpse = nullptr;
        SkinnableCorpseCheck check{ &bot };
        Trinity::CreatureSearcher<SkinnableCorpseCheck> searcher(&bot, corpse, check);
        Cell::VisitGridObjects(&bot, searcher, kGatherSearchYards);
        target = corpse;
    }
    else
    {
        GameObject* node = nullptr;
        GatherNodeCheck check{ &bot, GatherNodes(skill == SKILL_MINING ? LOCKTYPE_MINING : LOCKTYPE_HERBALISM), uint32(bot.GetSkillValue(skill)) };
        Trinity::GameObjectSearcher<GatherNodeCheck> searcher(&bot, node, check);
        Cell::VisitGridObjects(&bot, searcher, kGatherSearchYards);
        target = node;
    }
    if (!target)
    {
        reason = std::string("nothing to gather within ") + std::to_string(int(kGatherSearchYards)) + " yards";
        return BotActionStatus::kRefused;
    }
    if (!bot.IsWithinDist(target, kGatherReachYards))
    {
        // Walk to it: a point next to it, so the bot does not try to stand inside a rock. The next ask casts.
        float x, y, z;
        target->GetClosePoint(x, y, z, bot.GetCombatReach(), kGatherReachYards - 1.5f, target->GetAbsoluteAngle(&bot));
        bot.GetMotionMaster()->Clear();
        bot.GetMotionMaster()->MovePoint(0, x, y, z);
        reason = "walking to the " + std::string(skill == SKILL_SKINNING ? "corpse" : "node") + " " + std::to_string(int(bot.GetDistance(target))) + " yards away";
        return BotActionStatus::kMoving;
    }
    bot.StopMoving();
    return StatusForCast(bot.CastSpell(target, spell, false));
}

// Release the spirit (layout 11): the checks of WorldSession::HandleRepopRequest, which returns silently, plus the combat refusal.
BotActionStatus ReleaseSpiritAction(Player& bot, std::string& reason)
{
    if (bot.IsAlive())
    {
        reason = "bot is alive";
        return BotActionStatus::kRefused;
    }
    if (bot.HasPlayerFlag(PLAYER_FLAGS_GHOST))
    {
        reason = "spirit is already released";
        return BotActionStatus::kRefused;
    }
    if (bot.IsInCombat())
    {
        reason = "bot is in combat";
        return BotActionStatus::kRefused;
    }
    if (bot.IsBeingTeleported())
    {
        reason = "bot is being teleported";
        return BotActionStatus::kRefused;
    }
    if (bot.HasAuraType(SPELL_AURA_PREVENT_RESURRECTION))
    {
        reason = "an aura prevents resurrection";
        return BotActionStatus::kRefused;
    }
    WorldSession* session = bot.GetSession();
    if (!session)
    {
        reason = "bot has no session";
        return BotActionStatus::kRefused;
    }
    // Released as a ghost, never revived at the graveyard. HandleRepopRequest ends in RepopAtGraveyard, which places a body released inside
    // a dungeon or a raid at the graveyard alive (a ghost could not walk back to a corpse in there). A bot released that way stood up among
    // the wolves of Westfall at half health and died outside the dungeon. The contract of this action (tier2_layout kReleaseSpirit) is the
    // game's own release: the corpse stays where it fell and the ghost walks back, so the bot is revived in the dungeon it died in.
    if (bot.getDeathState() == JUST_DIED)
        bot.KillPlayer();   // a release that arrived after the kill but before the death update (the check of HandleRepopRequest)
    bot.RemovePet(nullptr, PET_SAVE_NOT_IN_SLOT, true);
    bot.BuildPlayerRepop();
    bot.RepopAtGraveyard(false);
    return BotActionStatus::kApplied;
}

// Answer a resurrect offer (layout 11): spell_id 0 accepts, 1 declines. The checks of WorldSession::HandleResurrectResponse.
BotActionStatus ResurrectResponseAction(Player& bot, BotAction const& action, std::string& reason)
{
    if (bot.IsAlive())
    {
        reason = "bot is alive";
        return BotActionStatus::kRefused;
    }
    Unit* caster = ResolveUnit(bot, action.target_guid);
    if (!caster || !bot.IsResurrectRequestedBy(caster->GetGUID()))
    {
        reason = "no resurrect pending from that player";
        return BotActionStatus::kInvalidTarget;
    }
    bool const decline = action.spell_id != 0;
    if (!decline)
    {
        if (Player* casterPlayer = caster->ToPlayer(); casterPlayer && casterPlayer->GetInstanceScript())
            if (InstanceScript* instance = casterPlayer->GetInstanceScript(); instance->IsEncounterInProgress() && !instance->GetCombatResurrectionCharges())
            {
                reason = "encounter in progress and no combat resurrection charge";
                return BotActionStatus::kRefused;
            }
    }
    WorldSession* session = bot.GetSession();
    if (!session)
    {
        reason = "bot has no session";
        return BotActionStatus::kRefused;
    }
    WorldPackets::Misc::ResurrectResponse response{ WorldPacket(CMSG_RESURRECT_RESPONSE) };
    response.Resurrecter = caster->GetGUID();
    response.Response = decline ? 1 : 0;
    session->HandleResurrectResponse(response);
    return BotActionStatus::kApplied;
}

// Reclaim the corpse (layout 11): the checks of WorldSession::HandleReclaimCorpse, which returns silently.
BotActionStatus ReclaimCorpseAction(Player& bot, std::string& reason)
{
    if (bot.IsAlive())
    {
        reason = "bot is alive";
        return BotActionStatus::kRefused;
    }
    if (!bot.HasPlayerFlag(PLAYER_FLAGS_GHOST))
    {
        reason = "spirit is not released";
        return BotActionStatus::kRefused;
    }
    if (bot.InArena())
    {
        reason = "no corpse reclaim in an arena";
        return BotActionStatus::kRefused;
    }
    Corpse* corpse = bot.GetCorpse();
    if (!corpse)
    {
        reason = "bot has no corpse in the world";
        return BotActionStatus::kRefused;
    }
    if (time_t(corpse->GetGhostTime() + bot.GetCorpseReclaimDelay(corpse->GetType() == CORPSE_RESURRECTABLE_PVP)) >
        time_t(GameTime::GetGameTime()))
    {
        reason = "corpse reclaim delay not over";
        return BotActionStatus::kBusy;
    }
    if (!corpse->IsWithinDistInMap(&bot, CORPSE_RECLAIM_RADIUS, true))
    {
        reason = "corpse is out of reclaim range or on another map";
        return BotActionStatus::kOutOfRange;
    }
    if (sConfigMgr->GetBoolDefault("Classic.Hardcore", false))
    {
        reason = "hardcore realm: death is permanent";
        return BotActionStatus::kRefused;
    }
    WorldSession* session = bot.GetSession();
    if (!session)
    {
        reason = "bot has no session";
        return BotActionStatus::kRefused;
    }
    WorldPackets::Misc::ReclaimCorpse request{ WorldPacket(CMSG_RECLAIM_CORPSE) };
    session->HandleReclaimCorpse(request);
    return BotActionStatus::kApplied;
}

// ---- Chat orders (kSummon, kRevive, kMount) ----

// The player an order names, when it is in the bot's own group (a bot obeys only its group; the module checks the leader).
Player* GroupSpeaker(Player& bot, uint64 ref, std::string& reason)
{
    Unit* unit = ResolveUnit(bot, ref);
    Player* speaker = unit ? unit->ToPlayer() : nullptr;
    if (!speaker || speaker == &bot)
    {
        reason = "no such player";
        return nullptr;
    }
    if (!bot.IsInSameGroupWith(speaker))
    {
        reason = "not in the bot's group";
        return nullptr;
    }
    return speaker;
}

// Puts the bot next to the speaker, on a spiral by the index (so several bots do not share one point). The rules of the foreverbots summon.
BotActionStatus MoveToSpeaker(Player& bot, Player& speaker, uint32 index, std::string& reason)
{
    if (!speaker.IsAlive())
    {
        reason = "the speaker is dead";
        return BotActionStatus::kRefused;
    }
    if (speaker.IsInCombat())
    {
        reason = "the speaker is in combat";
        return BotActionStatus::kRefused;
    }
    if (bot.IsInCombat())
    {
        reason = "bot is in combat";
        return BotActionStatus::kRefused;
    }
    bool const sameMap = bot.GetMapId() == speaker.GetMapId() && bot.GetInstanceId() == speaker.GetInstanceId();
    if (!sameMap)
    {
        Map const* botMap = bot.GetMap();
        Map const* speakerMap = speaker.GetMap();
        if ((botMap && botMap->Instanceable()) || (speakerMap && speakerMap->Instanceable()))
        {
            reason = "an instance is involved on another map";
            return BotActionStatus::kRefused;
        }
    }
    else if (bot.GetExactDist2d(&speaker) < 5.0f)
    {
        reason = "already beside the speaker";
        return BotActionStatus::kRefused;
    }

    float const angle = float(index) * 2.4f;
    float const radius = 2.5f + 0.9f * float(index);
    float const x = speaker.GetPositionX() + std::cos(angle) * radius;
    float const y = speaker.GetPositionY() + std::sin(angle) * radius;
    float z = speaker.GetPositionZ();
    if (sameMap)
    {
        float const ground = speaker.GetMap()->GetHeight(speaker.GetPhaseShift(), x, y, z + 2.0f);
        if (ground > INVALID_HEIGHT)
            z = ground;
    }
    if (!bot.TeleportTo(speaker.GetMapId(), x, y, z, speaker.GetOrientation()))
    {
        reason = "the teleport was refused";
        return BotActionStatus::kRefused;
    }
    return BotActionStatus::kApplied;
}

BotActionStatus SummonAction(Player& bot, BotAction const& action, std::string& reason)
{
    static bool const allowed = sConfigMgr->GetBoolDefault("Bot.Chat.Orders.Enabled", false);
    if (!allowed)
    {
        reason = "Bot.Chat.Orders.Enabled is off";
        return BotActionStatus::kRefused;
    }
    if (!bot.IsAlive())
    {
        reason = "bot is dead";
        return BotActionStatus::kRefused;
    }
    Player* speaker = GroupSpeaker(bot, action.target_guid, reason);
    if (!speaker)
        return BotActionStatus::kInvalidTarget;
    return MoveToSpeaker(bot, *speaker, uint32(std::max(0.0f, action.dest_x)), reason);
}

BotActionStatus ReviveAction(Player& bot, BotAction const& action, std::string& reason)
{
    static bool const allowed = sConfigMgr->GetBoolDefault("Bot.Chat.Orders.Enabled", false);
    static bool const sickness = sConfigMgr->GetBoolDefault("Bot.Chat.Orders.ReviveSickness", true);
    if (!allowed)
    {
        reason = "Bot.Chat.Orders.Enabled is off";
        return BotActionStatus::kRefused;
    }
    if (sConfigMgr->GetBoolDefault("Classic.Hardcore", false))
    {
        reason = "hardcore realm: death is permanent";
        return BotActionStatus::kRefused;
    }
    if (bot.IsAlive())
    {
        reason = "bot is not dead";
        return BotActionStatus::kRefused;
    }
    Player* speaker = GroupSpeaker(bot, action.target_guid, reason);
    if (!speaker)
        return BotActionStatus::kInvalidTarget;
    if (!speaker->IsAlive() || speaker->IsInCombat())
    {
        reason = "the speaker is dead or in combat";
        return BotActionStatus::kRefused;
    }
    bot.ResurrectPlayer(0.35f, sickness);
    bot.SpawnCorpseBones();
    std::string moveReason;
    BotActionStatus const moved = MoveToSpeaker(bot, *speaker, uint32(std::max(0.0f, action.dest_x)), moveReason);
    if (moved != BotActionStatus::kApplied)
        reason = "revived where it fell: " + moveReason; // the resurrect itself happened
    return BotActionStatus::kApplied;
}

BotActionStatus MountAction(Player& bot, BotAction const& action, std::string& reason)
{
    static bool const allowed = sConfigMgr->GetBoolDefault("Bot.Chat.Orders.Enabled", false);
    if (!allowed)
    {
        reason = "Bot.Chat.Orders.Enabled is off";
        return BotActionStatus::kRefused;
    }
    if (!bot.IsAlive())
    {
        reason = "bot is dead";
        return BotActionStatus::kRefused;
    }
    if (action.spell_id == 0)
    {
        if (!bot.IsMounted())
        {
            reason = "bot is not mounted";
            return BotActionStatus::kRefused;
        }
        bot.RemoveAurasByType(SPELL_AURA_MOUNTED);
        return BotActionStatus::kApplied;
    }
    if (bot.IsInCombat())
    {
        reason = "bot is in combat";
        return BotActionStatus::kRefused;
    }
    if (bot.IsMounted())
    {
        reason = "bot is already mounted";
        return BotActionStatus::kRefused;
    }

    // The fastest learned ground mount: a spell with a mounted aura and a mounted speed effect, flying mounts left out.
    uint32 bestSpell = 0;
    int32 bestSpeed = -1;
    for (auto const& [spellId, playerSpell] : bot.GetSpellMap())
    {
        if (playerSpell.state == PLAYERSPELL_REMOVED || playerSpell.disabled)
            continue;
        SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE);
        if (!info || !info->HasAura(SPELL_AURA_MOUNTED) || info->HasAura(SPELL_AURA_MOD_INCREASE_MOUNTED_FLIGHT_SPEED))
            continue;
        int32 speed = 0;
        for (SpellEffectInfo const& effect : info->GetEffects())
            if (effect.ApplyAuraName == SPELL_AURA_MOD_INCREASE_MOUNTED_SPEED)
                speed = std::max(speed, int32(effect.CalcValue(&bot)));
        if (speed > bestSpeed || (speed == bestSpeed && spellId < bestSpell))
        {
            bestSpell = spellId;
            bestSpeed = speed;
        }
    }
    if (bestSpell == 0)
    {
        reason = "bot has no mount";
        return BotActionStatus::kRefused;
    }
    SpellCastResult const result = bot.CastSpell(&bot, bestSpell, false);
    if (result != SPELL_CAST_OK)
    {
        reason = "cast of mount " + std::to_string(bestSpell) + " returned result " + std::to_string(int32(result));
        return StatusForCast(result);
    }
    return BotActionStatus::kApplied;
}

// Whether the navmesh has a full route to (x, y, z) from where the bot stands now (layout 13, BotActionStatus::kNoRoute).
//
// The move itself is never refused over this: PointMovementGenerator falls back to a straight line when there is no path,
// so the bot walks to the nearest reachable point and stops there, exactly as the status documents. Only the answer
// changes, so a module can drop a destination it can never reach instead of walking into a wall for ever.
//
// Three cases read as "no route", and one deliberately does not:
//   PATHFIND_NOPATH      - the start or the destination is not on the mesh at all.
//   PATHFIND_INCOMPLETE  - a partial path only: the mesh runs out before the destination.
//   PATHFIND_FARFROMPOLY - start or destination further than 7 yards from any polygon, flagged on top of the two above.
// PATHFIND_NOT_USING_PATH is NOT a missing route: it is what PathGenerator returns when there is no navmesh to use (no
// mmaps loaded for the map, or the unit flies, swims or falls), where a straight line is the intended movement. Reporting
// kNoRoute there would make every destination on such a map "unreachable" and a module would stop sending moves at all.
bool HasFullRouteTo(Player& bot, float x, float y, float z)
{
    PathGenerator path(&bot);
    if (!path.CalculatePath(x, y, z, false))
        return false; // a coordinate the map rejects outright: nothing can get there

    PathType const type = path.GetPathType();
    if (type & PATHFIND_NOT_USING_PATH)
        return true;
    return !((type & (PATHFIND_NOPATH | PATHFIND_INCOMPLETE)) != 0);
}

// Use a game object (layout 19, ActionType::kUseObject): a door, a lever, an altar, a chest, as a click would. spell_id is the object's entry; the nearest of that
// entry within 30 yards is the one, and the bot must be within kUseObjectYards of it. target_guid, when not 0, is an item entry: the bot gets one if it has none
// and uses it on the object (the item's own spell is cast at the object), which is how a door that only a key or a charge opens is opened. A scenario tool: off
// unless Bot.Actions.UseObject is on.
constexpr float kUseObjectSearchYards = 30.0f;
constexpr float kUseObjectYards = 8.0f;

BotActionStatus UseObjectAction(Player& bot, BotAction const& action, std::string& reason)
{
    static bool const allowed = sConfigMgr->GetBoolDefault("Bot.Actions.UseObject", false);
    if (!allowed)
    {
        reason = "Bot.Actions.UseObject is off";
        return BotActionStatus::kRefused;
    }
    if (!bot.IsAlive() || bot.IsInCombat() || bot.IsInFlight())
    {
        reason = !bot.IsAlive() ? "bot is dead" : bot.IsInCombat() ? "bot is in combat" : "bot is on a flight";
        return BotActionStatus::kRefused;
    }
    GameObject* object = action.spell_id ? bot.FindNearestGameObject(action.spell_id, kUseObjectSearchYards, false) : nullptr;
    if (!object)
    {
        reason = "no object " + std::to_string(action.spell_id) + " within " + std::to_string(int(kUseObjectSearchYards)) + " yards";
        return BotActionStatus::kInvalidTarget;
    }
    if (!bot.IsWithinDistInMap(object, kUseObjectYards))
    {
        reason = "the object is " + std::to_string(int(bot.GetDistance(object))) + " yards away";
        return BotActionStatus::kOutOfRange;
    }

    uint32 const itemEntry = uint32(action.target_guid);
    if (itemEntry == 0)
    {
        // A door or a lever toggles when used: one that is open (or pressed) already is left alone, so a bot that comes by twice does not shut it.
        GameobjectTypes const type = object->GetGoType();
        if ((type == GAMEOBJECT_TYPE_DOOR || type == GAMEOBJECT_TYPE_BUTTON) && object->GetGoState() == GO_STATE_ACTIVE)
            return BotActionStatus::kApplied;
        object->Use(&bot);
        return BotActionStatus::kApplied;
    }

    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemEntry);
    if (!proto || proto->Effects.empty())
    {
        reason = "item " + std::to_string(itemEntry) + " is unknown or has no use effect";
        return BotActionStatus::kRefused;
    }
    bool lockRequiresItem = false;
    if (uint32 const lockId = object->GetGOInfo()->GetLockId())
        if (LockEntry const* lock = sLockStore.LookupEntry(lockId))
            for (uint8 i = 0; i < MAX_LOCK_CASE; ++i)
                if (lock->Type[i] == LOCK_KEY_ITEM && uint32(lock->Index[i]) == itemEntry)
                    lockRequiresItem = true;
    if (!lockRequiresItem)
    {
        reason = "the object's lock does not require item " + std::to_string(itemEntry);
        return BotActionStatus::kRefused;
    }
    Item* item = bot.GetItemByEntry(itemEntry);
    if (!item)
    {
        ItemPosCountVec destination;
        if (bot.CanStoreNewItem(NULL_BAG, NULL_SLOT, destination, itemEntry, 1) != EQUIP_ERR_OK || !(item = bot.StoreNewItem(destination, itemEntry, true)))
        {
            reason = "the bot cannot be given item " + std::to_string(itemEntry);
            return BotActionStatus::kRefused;
        }
    }
    if (Spell const* current = bot.GetCurrentSpell(CURRENT_GENERIC_SPELL); current && current->getState() != SPELL_STATE_FINISHED)
    {
        reason = "bot is casting spell " + std::to_string(current->GetSpellInfo()->Id);
        return BotActionStatus::kBusy;
    }
    if (bot.isMoving())
    {
        reason = "bot is moving; stop first";
        return BotActionStatus::kMoving;
    }
    CastSpellExtraArgs args(TRIGGERED_NONE);
    args.SetCastItem(item);
    SpellCastResult const result = bot.CastSpell(object, uint32(proto->Effects[0]->SpellID), args);
    if (result != SPELL_CAST_OK)
        reason = "use of item " + std::to_string(itemEntry) + " on the object returned result " + std::to_string(int32(result));
    return StatusForCast(result);
}

// Fly (layout 17, ActionType::kTaxi). The bot stands at a flight master (target_guid names it, 0 = the nearest in reach) and wants to get near
// (dest_x, dest_y, dest_z) on its own map (spell_id is that map). Every bot knows every flight node of its faction (BotTaxi::LearnAllNodes, also
// done at login; the player's handler checks the mask, this path does not, so a missing node could not refuse the flight) and flies free
// (Player::ActivateTaxiPathTo does not charge a bot, BotTaxi::FliesFree). The flight goes from the node at the flight master to the node
// nearest to the point, by the shortest route of the faction's graph, and is refused when it would not bring the bot at least Bot.Taxi.MinGainYards closer.
BotActionStatus TaxiAction(Player& bot, BotAction const& action, std::string& reason)
{
    if (!BotTaxi::Enabled())
    {
        reason = "Bot.Taxi.Enabled is off";
        return BotActionStatus::kRefused;
    }
    if (!bot.IsAlive() || bot.IsInCombat() || bot.IsInFlight())
    {
        reason = !bot.IsAlive() ? "bot is dead" : bot.IsInCombat() ? "bot is in combat" : "bot is on a flight already";
        return BotActionStatus::kRefused;
    }
    if (action.spell_id != bot.GetMapId())
    {
        reason = "the point is on map " + std::to_string(action.spell_id) + ", the bot is on map " + std::to_string(bot.GetMapId());
        return BotActionStatus::kRefused;
    }

    Creature* master = nullptr;
    if (Unit* named = action.target_guid ? ResolveUnit(bot, action.target_guid) : nullptr)
        if (Creature* creature = named->ToCreature();
            creature && creature->IsInWorld() && creature->IsAlive() &&
            bot.GetNPCIfCanInteractWith(creature->GetGUID(), UNIT_NPC_FLAG_FLIGHTMASTER, UNIT_NPC_FLAG_2_NONE))
            master = creature;
    if (!master)
        master = NearestServiceNpc(bot, UNIT_NPC_FLAG_FLIGHTMASTER);
    if (!master)
    {
        reason = "no flight master within interaction range";
        return BotActionStatus::kRefused;
    }

    uint32 const team = bot.GetTeam();
    uint32 const fromId = sObjectMgr->GetNearestTaxiNode(master->GetPositionX(), master->GetPositionY(), master->GetPositionZ(), master->GetMapId(), team);
    uint32 const toId = sObjectMgr->GetNearestTaxiNode(action.dest_x, action.dest_y, action.dest_z, action.spell_id, team);
    TaxiNodesEntry const* from = fromId ? sTaxiNodesStore.LookupEntry(fromId) : nullptr;
    TaxiNodesEntry const* to = toId ? sTaxiNodesStore.LookupEntry(toId) : nullptr;
    if (!from || !to)
    {
        reason = "no flight node near the flight master or near the point";
        return BotActionStatus::kRefused;
    }
    if (from->ID == to->ID)
    {
        reason = "the nearest flight node to the point is the one the bot stands at";
        return BotActionStatus::kRefused;
    }

    // Worth it: the landing must be closer to the point than the flight master is, by at least the gain.
    float const here = std::hypot(master->GetPositionX() - action.dest_x, master->GetPositionY() - action.dest_y);
    float const landing = std::hypot(to->Pos.X - action.dest_x, to->Pos.Y - action.dest_y);
    if (landing + BotTaxi::MinGainYards() > here)
    {
        reason = "a flight would bring the bot only " + std::to_string(int32(here - landing)) + " yards closer";
        return BotActionStatus::kRefused;
    }

    BotTaxi::LearnAllNodes(bot);    // again if the mask lost a node (a bot that was not set up at login, or an older save)

    std::vector<uint32> nodes;
    TaxiPathGraph::GetCompleteNodeRoute(from, to, &bot, nodes);
    if (nodes.size() < 2)
    {
        reason = "no flight path from node " + std::to_string(from->ID) + " to node " + std::to_string(to->ID);
        return BotActionStatus::kRefused;
    }
    if (!bot.ActivateTaxiPathTo(nodes, master))
    {
        reason = "the flight was refused (busy, shapeshifted or casting)";
        return BotActionStatus::kRefused;
    }
    return BotActionStatus::kApplied;
}

// Layout 16 adds potions and bandages: spell_id 3 uses the best health potion, 4 the best mana potion (both allowed in combat and on the move,
// as for a player; the item's cooldown applies), 5 the best bandage (out of combat, standing still).
// Eat or drink (layout 15, ActionType::kConsume). spell_id 1 uses the best food the bot carries, 2 the best drink. The item's own use spell is
// cast with the item attached, so the bot sits, restores over time and uses up one piece, as it does for a player. The module stops the bot
// and keeps it out of fights first: a spell that is interrupted by movement or combat wastes the piece.
BotActionStatus ConsumeAction(Player& bot, BotAction const& action, std::string& reason)
{
    if (!ModuleBotSupplies::Enabled())
    {
        reason = "Bot.Actions.Supplies is off";
        return BotActionStatus::kRefused;
    }
    ModuleBotSupplies::Kind kind = ModuleBotSupplies::Kind::None;
    if (action.spell_id == 1)
        kind = ModuleBotSupplies::Kind::Food;
    else if (action.spell_id == 2)
        kind = ModuleBotSupplies::Kind::Drink;
    else if (action.spell_id == 3)
        kind = ModuleBotSupplies::Kind::HealthPotion;
    else if (action.spell_id == 4)
        kind = ModuleBotSupplies::Kind::ManaPotion;
    else if (action.spell_id == 5)
        kind = ModuleBotSupplies::Kind::Bandage;
    else
    {
        reason = "unknown consume kind " + std::to_string(action.spell_id);
        return BotActionStatus::kRefused;
    }
    bool const potion = kind == ModuleBotSupplies::Kind::HealthPotion || kind == ModuleBotSupplies::Kind::ManaPotion;
    if (!bot.IsAlive() || (bot.IsInCombat() && !potion))
    {
        reason = bot.IsAlive() ? "bot is in combat" : "bot is dead";
        return BotActionStatus::kRefused;
    }
    if (potion)
    {
        // A potion is drunk at once, in a fight or on the move; only a spell in progress stops it.
        Item* drink = ModuleBotSupplies::BestCarried(bot, kind);
        if (!drink || drink->GetTemplate()->Effects.empty())
        {
            reason = kind == ModuleBotSupplies::Kind::HealthPotion ? "the bot carries no health potion it can use" : "the bot carries no mana potion it can use";
            return BotActionStatus::kRefused;
        }
        if (Spell const* current = bot.GetCurrentSpell(CURRENT_GENERIC_SPELL); current && current->getState() != SPELL_STATE_FINISHED)
        {
            reason = "bot is casting spell " + std::to_string(current->GetSpellInfo()->Id);
            return BotActionStatus::kBusy;
        }
        uint32 const potionSpell = uint32(drink->GetTemplate()->Effects[0]->SpellID);
        if (bot.GetSpellHistory()->HasCooldown(potionSpell, drink->GetEntry()))
        {
            reason = "the potion is on cooldown";
            return BotActionStatus::kRefused;
        }
        CastSpellExtraArgs potionArgs(TRIGGERED_NONE);
        potionArgs.SetCastItem(drink);
        SpellCastResult const potionResult = bot.CastSpell(&bot, potionSpell, potionArgs);
        if (potionResult != SPELL_CAST_OK)
            reason = "use of item " + std::to_string(drink->GetEntry()) + " returned result " + std::to_string(int32(potionResult));
        return StatusForCast(potionResult);
    }
    if (Spell const* current = bot.GetCurrentSpell(CURRENT_GENERIC_SPELL); current && current->getState() != SPELL_STATE_FINISHED)
    {
        reason = "bot is casting spell " + std::to_string(current->GetSpellInfo()->Id);
        return BotActionStatus::kBusy;
    }
    if (bot.isMoving())
    {
        reason = "bot is moving; stop first";
        return BotActionStatus::kMoving;
    }

    Item* item = ModuleBotSupplies::BestCarried(bot, kind);
    if (!item || item->GetTemplate()->Effects.empty())
    {
        reason = kind == ModuleBotSupplies::Kind::Food ? "the bot carries no food it can eat"
            : kind == ModuleBotSupplies::Kind::Bandage ? "the bot carries no bandage it can use" : "the bot carries no drink it can use";
        return BotActionStatus::kRefused;
    }

    uint32 const spellId = uint32(item->GetTemplate()->Effects[0]->SpellID);
    CastSpellExtraArgs args(TRIGGERED_NONE);
    args.SetCastItem(item);
    SpellCastResult const result = bot.CastSpell(&bot, spellId, args);
    if (result != SPELL_CAST_OK)
        reason = "use of item " + std::to_string(item->GetEntry()) + " (spell " + std::to_string(spellId) + ") returned result " + std::to_string(int32(result));
    return StatusForCast(result);
}

// Pet commands (layout 13, ActionType::kPetCommand). spell_id: 1 attack target_guid, 2 follow the bot, 3 stay, 4 set the
// stance (in dest_x: 0 passive, 1 defensive, 2 aggressive). Each mirrors what the pet bar does for a player (the handlers
// in PetHandler.cpp), so the pet AI reads the order the same way it reads one from a client.
BotActionStatus PetCommandAction(Player& bot, BotAction const& action, std::string& reason)
{
    if (bot.IsInCombat())
    {
        reason = "bot is in combat";
        return BotActionStatus::kRefused;
    }

    Pet* pet = bot.GetPet();
    if (!pet || !pet->IsInWorld() || !pet->IsAlive())
    {
        reason = "bot has no pet out";
        return BotActionStatus::kInvalidTarget;
    }
    CharmInfo* charm = pet->GetCharmInfo();
    if (!charm)
    {
        reason = "the bot's pet has no command state";
        return BotActionStatus::kRefused;
    }

    switch (action.spell_id)
    {
        case 1: // attack
        {
            Unit* target = ResolveUnit(bot, action.target_guid);
            if (!target || target == &bot || !target->IsInWorld() || !target->IsAlive())
            {
                reason = "pet command target is not a live unit on this bot's map";
                return BotActionStatus::kInvalidTarget;
            }
            // The same rule the pet bar applies for a player: the owner must be allowed to attack the unit, and neither
            // the pet nor the target may be under a charm (a charmed unit attacks whoever holds it, not its master).
            Player* owner = pet->GetOwner();
            if (!owner || !owner->IsValidAttackTarget(target) || pet->IsCharmed() || target->IsCharmed())
            {
                reason = "the bot's pet may not attack that unit";
                return BotActionStatus::kInvalidTarget;
            }
            // The order is read by the pet AI (PetAI reads CharmInfo), so it goes in the same way the pet bar puts it in
            // (WorldSession::HandlePetActionHelper, COMMAND_ATTACK): through the AI, so a target switch is forced.
            if (pet->GetVictim() != target || !charm->IsCommandAttack())
            {
                if (pet->GetVictim())
                    pet->AttackStop();

                charm->SetIsCommandAttack(true);
                charm->SetIsAtStay(false);
                charm->SetIsFollowing(false);
                charm->SetIsCommandFollow(false);
                charm->SetIsReturning(false);

                if (CreatureAI* ai = pet->ToCreature()->AI())
                {
                    if (PetAI* petAI = dynamic_cast<PetAI*>(ai))
                        petAI->_AttackStart(target); // force the target switch
                    else
                        ai->AttackStart(target);
                }
                else
                    pet->Attack(target, true);
            }
            return BotActionStatus::kApplied;
        }

        case 2: // follow
            pet->AttackStop();
            pet->InterruptSpell(CURRENT_GENERIC_SPELL, false, false);
            if (Spell const* channeled = pet->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
                channeled && !channeled->GetSpellInfo()->HasAttribute(SPELL_ATTR9_CHANNEL_PERSISTS_ON_PET_FOLLOW))
                pet->InterruptSpell(CURRENT_CHANNELED_SPELL, true, true);
            pet->GetMotionMaster()->MoveFollow(&bot, PET_FOLLOW_DIST, pet->GetFollowAngle());

            charm->SetCommandState(COMMAND_FOLLOW);
            charm->SetIsCommandAttack(false);
            charm->SetIsAtStay(false);
            charm->SetIsReturning(true);
            charm->SetIsCommandFollow(true);
            charm->SetIsFollowing(false);
            return BotActionStatus::kApplied;

        case 3: // stay
            pet->GetMotionMaster()->Clear(MOTION_PRIORITY_NORMAL);
            pet->GetMotionMaster()->MoveIdle();

            charm->SetCommandState(COMMAND_STAY);
            charm->SetIsCommandAttack(false);
            charm->SetIsAtStay(true);
            charm->SetIsCommandFollow(false);
            charm->SetIsFollowing(false);
            charm->SetIsReturning(false);
            charm->SaveStayPosition();
            return BotActionStatus::kApplied;

        case 4: // stance
        {
            if (action.dest_x > 2.0f)
            {
                reason = "unknown pet stance " + std::to_string(action.dest_x) + " (0 passive, 1 defensive, 2 aggressive)";
                return BotActionStatus::kRefused;
            }
            pet->SetReactState(action.dest_x < 0.5f ? REACT_PASSIVE : action.dest_x < 1.5f ? REACT_DEFENSIVE : REACT_AGGRESSIVE);
            return BotActionStatus::kApplied;
        }

        default:
            reason = "unknown pet command " + std::to_string(action.spell_id);
            return BotActionStatus::kRefused;
    }
}

BotActionStatus ApplyBotAction(Player& bot, BotAction const& action, std::string& reason, uint32 module)
{
    switch (action.type)
    {
        case ActionType::kStop:
            bot.StopMoving();
            bot.GetMotionMaster()->Clear();
            bot.GetMotionMaster()->MoveIdle();
            return BotActionStatus::kApplied;

        case ActionType::kMoveTo:
        {
            // The route is asked for before the move is started, and the move is carried out either way: a bot with no full
            // route still walks as far towards the destination as the mesh allows. kNoRoute only tells the module that its
            // destination is out of reach, so it stops sending the bot there (layout 13, BotActionStatus::kNoRoute).
            if (!Trinity::IsValidMapCoord(action.dest_x, action.dest_y, action.dest_z))
            {
                reason = "not a valid map position";
                return BotActionStatus::kRefused;
            }
            bool const reachable = HasFullRouteTo(bot, action.dest_x, action.dest_y, action.dest_z);
            bot.GetMotionMaster()->Clear();
            bot.GetMotionMaster()->MovePoint(0, action.dest_x, action.dest_y, action.dest_z);
            if (!reachable)
            {
                reason = "no full route to (" + std::to_string(action.dest_x) + ", " + std::to_string(action.dest_y) +
                         ", " + std::to_string(action.dest_z) + "): the bot walks to the nearest reachable point";
                return BotActionStatus::kNoRoute;
            }
            return BotActionStatus::kApplied;
        }

        case ActionType::kFollow:
        {
            Unit* target = ResolveUnit(bot, action.target_guid);
            if (!target || target == &bot || !target->IsInWorld())
            {
                reason = "follow target is not a unit on this bot's map";
                return BotActionStatus::kInvalidTarget;
            }
            bot.GetMotionMaster()->Clear();
            bot.GetMotionMaster()->MoveFollow(target, 1.0f);
            return BotActionStatus::kApplied;
        }

        case ActionType::kAttackTarget:
        {
            Unit* target = ResolveHostileTarget(bot, action.target_guid, reason);
            if (!target)
                return BotActionStatus::kInvalidTarget;
            // Already meleeing this target: Unit::Attack refuses a repeat, but the bot is attacking as asked.
            if (bot.GetVictim() == target && bot.HasUnitState(UNIT_STATE_MELEE_ATTACKING))
                return BotActionStatus::kApplied;
            if (!bot.Attack(target, true))
            {
                reason = "attack refused: " + AttackRefusalReason(bot, target);
                return BotActionStatus::kRefused;
            }
            bot.SetFacingToObject(target); // a swing and most spells need the target in front
            return BotActionStatus::kApplied;
        }

        case ActionType::kVendor:
        {
            if (bot.IsInCombat())
            {
                reason = "bot is in combat";
                return BotActionStatus::kRefused;
            }

            uint32 const services = action.spell_id;
            bool const buying = (services & kServiceBuyMask) != 0 && ModuleBotSupplies::Enabled();
            if (services & (kServiceSellBit | kServiceSellJunkBit | kServiceRepairBit) || buying)
            {
                // Layout 13: the module can name the NPC (BotSnapshot::vendor_guid / repair_guid, which is where a BuyItem
                // action gets its vendor from). A reference the core cannot use is not an error -- the nearest NPC in
                // interaction range is used instead, which is what a core before layout 13 relied on anyway.
                Creature* npc = nullptr;
                if (Unit* named = action.target_guid ? ResolveUnit(bot, action.target_guid) : nullptr)
                    if (Creature* creature = named->ToCreature();
                        creature && creature->IsInWorld() && creature->IsAlive() &&
                        bot.GetNPCIfCanInteractWith(creature->GetGUID(), UNIT_NPC_FLAG_VENDOR_MASK | UNIT_NPC_FLAG_REPAIR,
                                                    UNIT_NPC_FLAG_2_NONE))
                        npc = creature;
                if (!npc)
                    npc = NearestServiceNpc(bot, UNIT_NPC_FLAG_VENDOR_MASK | UNIT_NPC_FLAG_REPAIR);
                if (!npc)
                {
                    reason = "no vendor or repairman within interaction range";
                    return BotActionStatus::kRefused;
                }
                if ((services & (kServiceSellBit | kServiceSellJunkBit)) && npc->HasNpcFlag(UNIT_NPC_FLAG_VENDOR_MASK))
                    SellGreyItems(bot, (services & kServiceSellJunkBit) != 0);
                if ((services & kServiceRepairBit) && npc->HasNpcFlag(UNIT_NPC_FLAG_REPAIR))
                    bot.DurabilityRepairAll(true, 0.0f, false);
                // Buying comes last, from the money the sale left and the repair did not take. A vendor without the goods buys nothing and
                // is not an error: the module sees it in the counts of the next snapshot.
                if (buying && npc->HasNpcFlag(UNIT_NPC_FLAG_VENDOR_MASK))
                {
                    auto const target = [](float value) { return uint32(std::clamp(value, 0.0f, kMaxSupplyTarget)); };
                    ModuleBotSupplies::Targets targets;
                    if (services & kServiceBuyAmmoBit)
                        targets.Ammo = target(action.dest_x);
                    if (services & kServiceBuyFoodBit)
                        targets.Food = target(action.dest_y);
                    if (services & kServiceBuyDrinkBit)
                        targets.Drink = target(action.dest_z);
                    if (services & kServiceBuyHealthPotionBit)
                        targets.HealthPotion = ModuleBotSupplies::HealthPotionTarget();
                    if (services & kServiceBuyManaPotionBit)
                        targets.ManaPotion = ModuleBotSupplies::ManaPotionTarget();
                    if (services & kServiceBuyBandageBit)
                        targets.Bandage = ModuleBotSupplies::BandageTarget();
                    if (services & kServiceBuyReagentBit)
                    {
                        ModuleBotSupplies::MissingReagent const missing = ModuleBotSupplies::MissingReagentOf(bot);
                        targets.ReagentEntry = missing.Entry;
                        targets.Reagent = std::max(ModuleBotSupplies::ReagentTarget(), missing.Needed);
                    }
                    ModuleBotSupplies::Buy(bot, npc, targets);
                }
            }
            if (services & kServiceEquipBit)
                EquipBestGear(bot);
            return BotActionStatus::kApplied;
        }

        case ActionType::kSkillUp:
        {
            bool refused = false;
            BotActionStatus const blocked = CannotCraftNow(bot, reason, refused);
            if (refused)
                return blocked;
            if (!IsCraftSkill(action.spell_id))
            {
                reason = "skill " + std::to_string(action.spell_id) + " is not one the bots craft (129 First Aid, 185 Cooking, 164 Blacksmithing, 165 Leatherworking, 171 Alchemy, "
                         "186 Smelting, 197 Tailoring, 202 Engineering)";
                return BotActionStatus::kRefused;
            }
            uint32 needsFocus = 0;
            uint32 const recipe = ChooseSkillUpRecipe(bot, action.spell_id, reason, needsFocus);
            if (!recipe)
                return needsFocus ? BotActionStatus::kNeedsFocus : BotActionStatus::kRefused;
            return CastRecipe(bot, recipe, reason);
        }

        case ActionType::kSellToVendor:
        {
            if (bot.IsInCombat())
            {
                reason = "bot is in combat";
                return BotActionStatus::kRefused;
            }
            Creature* npc = nullptr;
            if (Unit* named = action.target_guid ? ResolveUnit(bot, action.target_guid) : nullptr)
                if (Creature* creature = named->ToCreature();
                    creature && creature->IsInWorld() && creature->IsAlive() &&
                    bot.GetNPCIfCanInteractWith(creature->GetGUID(), UNIT_NPC_FLAG_VENDOR_MASK, UNIT_NPC_FLAG_2_NONE))
                    npc = creature;
            if (!npc)
                npc = NearestServiceNpc(bot, UNIT_NPC_FLAG_VENDOR_MASK);
            if (!npc)
            {
                reason = "no vendor within interaction range";
                return BotActionStatus::kRefused;
            }
            uint32 const count = uint32(std::clamp(action.dest_x, 0.0f, kMaxSupplyTarget));
            if (SellEntryToVendor(bot, action.spell_id, count) == 0)
            {
                reason = "nothing of that item to sell to this vendor";
                return BotActionStatus::kRefused;
            }
            return BotActionStatus::kApplied;
        }

        case ActionType::kTrain:
        {
            if (bot.IsInCombat())
            {
                reason = "bot is in combat";
                return BotActionStatus::kRefused;
            }
            return LearnFromTrainer(bot, reason);
        }

        case ActionType::kRest:
        {
            // spell_id 1 sits down to rest, 0 stands up. A bot in combat does not sit.
            if (action.spell_id == 0)
            {
                bot.SetStandState(UNIT_STAND_STATE_STAND);
                return BotActionStatus::kApplied;
            }
            if (bot.IsInCombat())
            {
                reason = "bot is in combat and cannot sit";
                return BotActionStatus::kRefused;
            }
            bot.SetStandState(UNIT_STAND_STATE_SIT);
            return BotActionStatus::kApplied;
        }

        case ActionType::kLoot:
            return LootCorpse(bot, action.target_guid, reason);

        case ActionType::kReleaseSpirit:
            return ReleaseSpiritAction(bot, reason);

        case ActionType::kSummon:
            return SummonAction(bot, action, reason);

        case ActionType::kRevive:
            return ReviveAction(bot, action, reason);

        case ActionType::kMount:
            return MountAction(bot, action, reason);

        case ActionType::kResurrect:
            return ResurrectResponseAction(bot, action, reason);

        case ActionType::kReclaimCorpse:
            return ReclaimCorpseAction(bot, reason);

        case ActionType::kQuest:
            return QuestAction(bot, action.spell_id, reason);

        case ActionType::kTeleport:
            return TeleportAction(bot, action, reason);

        case ActionType::kGrant:
            return GrantAction(bot, action, reason);

        case ActionType::kAutoGear:
            return AutoGearAction(bot, action.spell_id, reason);

        case ActionType::kPetCommand:
            return PetCommandAction(bot, action, reason);

        case ActionType::kConsume:
            return ConsumeAction(bot, action, reason);

        case ActionType::kTaxi:
            return TaxiAction(bot, action, reason);

        case ActionType::kUseObject:
            return UseObjectAction(bot, action, reason);

        // Crafting and gathering (SDK 0.18).
        case ActionType::kCraft:
            return CraftAction(bot, action, reason);

        case ActionType::kGather:
            return GatherAction(bot, action, reason);

        case ActionType::kGroup:
        {
            // Forming and changing a party is out of combat only, the same as the trainer and vendor actions.
            if (bot.IsInCombat())
            {
                reason = "bot is in combat";
                return BotActionStatus::kRefused;
            }
            BotActionStatus status = BotActionStatus::kRefused;
            switch (action.spell_id)
            {
                case 1: status = InviteToParty(bot, action.target_guid, reason, module); break;
                case 2: status = AcceptPartyInvite(bot, reason); break;
                case 3: status = LeaveParty(bot, reason); break;
                // 4/5: 40-man raid support (doc 108). No new ActionType or wire field: spell_id 4 and dest_x reuse
                // the existing generic kGroup fields the same way kVendor/kPetCommand already overload spell_id, so
                // this needed no SDK protocol bump (the BotAction struct itself is unchanged).
                case 4: status = ConvertGroupToRaid(bot, reason); break;
                case 5:
                {
                    uint8 subgroup = 0;
                    if (!ModuleGroupRules::TryDecodeSubgroup(action.dest_x, MAX_RAID_SUBGROUPS, subgroup))
                    {
                        reason = "subgroup out of range or not an integer";
                        status = BotActionStatus::kInvalidTarget;
                        break;
                    }
                    status = AssignSubgroup(bot, action.target_guid, subgroup, reason);
                    break;
                }
                default: reason = "unknown party action " + std::to_string(action.spell_id); break;
            }
            // Diagnostic: the bot module only sees "refused", so the reason is logged here.
            if (status == BotActionStatus::kRefused)
                TC_LOG_INFO("server.worldserver", "BotMgr: party action {} refused for bot={}: {}", action.spell_id, bot.GetName(), reason);
            return status;
        }

        case ActionType::kCastSpell:
        {
            if (!sSpellMgr->GetSpellInfo(action.spell_id, DIFFICULTY_NONE))
            {
                reason = "unknown spell " + std::to_string(action.spell_id);
                return BotActionStatus::kUnknownSpell;
            }
            // The module names a rank 1 spell; the bot casts the highest rank it knows.
            uint32 const castId = HighestKnownRank(bot, action.spell_id);
            if (!castId)
            {
                reason = "bot does not know spell " + std::to_string(action.spell_id);
                return BotActionStatus::kNotKnown;
            }
            // A zero target means the bot casts on itself.
            Unit* target = action.target_guid == 0 ? &bot : ResolveUnit(bot, action.target_guid);
            if (!target || !target->IsInWorld())
            {
                reason = "cast target is not a unit on this bot's map";
                return BotActionStatus::kInvalidTarget;
            }
            // Known to fail: a cast while another is in progress, and a spell with a cast time while moving. The module is told which, and
            // waits or stops, instead of getting a generic refusal from the spell system.
            if (Spell const* current = bot.GetCurrentSpell(CURRENT_GENERIC_SPELL); current && current->getState() != SPELL_STATE_FINISHED)
            {
                reason = "bot is casting spell " + std::to_string(current->GetSpellInfo()->Id);
                return BotActionStatus::kBusy;
            }
            if (Spell const* channel = bot.GetCurrentSpell(CURRENT_CHANNELED_SPELL); channel && channel->getState() != SPELL_STATE_FINISHED)
            {
                reason = "bot is channeling spell " + std::to_string(channel->GetSpellInfo()->Id);
                return BotActionStatus::kBusy;
            }
            if (bot.isMoving())
            {
                SpellInfo const* castInfo = sSpellMgr->GetSpellInfo(castId, DIFFICULTY_NONE);
                if (castInfo && castInfo->CalcCastTime() > 0)
                {
                    reason = "spell " + std::to_string(castId) + " has a cast time and the bot is moving";
                    return BotActionStatus::kMoving;
                }
            }
            if (target != &bot && !bot.HasInArc(3.14159265f, target))
                bot.SetFacingToObject(target); // spells aimed at a unit need it in front; the client would turn the caster
            SpellCastResult const result = bot.CastSpell(target, castId, false);
            if (result != SPELL_CAST_OK)
                reason = "cast of spell " + std::to_string(castId) + " returned result " + std::to_string(int32(result));
            if (result == SPELL_FAILED_REAGENTS)
                ModuleBotSupplies::NoteMissingReagent(bot, castId); // layout 16: the snapshot now names the item and a vendor that sells it
            return StatusForCast(result);
        }
    }

    reason = "unknown action type";
    return BotActionStatus::kRefused;
}

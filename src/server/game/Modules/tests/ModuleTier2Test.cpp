// Tier 2 region: the core creates it, a module-side reader maps it by name and reads what was published.
// Built by hand like the other module tests (see ModuleSessionTest.cpp for the command line, with
// ModuleTier2.cpp added and -lrt on older glibc).

#include "ModuleTier2.h"

#include <vector>

#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/shared_memory_object.hpp>

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace bi = boost::interprocess;

namespace
{

int g_failures = 0;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (!(cond)) {                                                              \
            ++g_failures;                                                           \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
        }                                                                           \
    } while (0)

} // namespace

int main()
{
    // The core writes kTier2LayoutVersion into the region and the module refuses to attach on any other value, so the
    // number is pinned here as well as compared below: a silent bump of the core's copy would otherwise only show up as a
    // module that refuses to attach.
    CHECK(forever_module::kTier2LayoutVersion == 19);
    CHECK(static_cast<int>(forever_module::BotActionStatus::kNoRoute) == 13);
    CHECK(static_cast<int>(forever_module::ActionType::kCraft) == 20);
    CHECK(static_cast<int>(forever_module::ActionType::kGather) == 21);
    CHECK(static_cast<int>(forever_module::ActionType::kPetCommand) == 22);
    CHECK(static_cast<int>(forever_module::ActionType::kConsume) == 23);
    CHECK(static_cast<int>(forever_module::ActionType::kLastActionType) ==
          static_cast<int>(forever_module::ActionType::kConsume));
    CHECK(forever_module::kMaxAuras == 16);

    // The shared region is mapped by two processes built separately, so a shape that changes without a layout bump is
    // memory corruption that nothing else would catch. These sizes are what the SDK's header gives on the same platform.
    CHECK(sizeof(forever_module::AuraEntry) == 12);
    CHECK(sizeof(forever_module::BotSnapshot) == 712);
    CHECK(sizeof(forever_module::NearbyUnit) == 48);
    CHECK(offsetof(forever_module::BotSnapshot, aura_count) == 376);
    CHECK(offsetof(forever_module::BotSnapshot, auras) == 380);
    CHECK(offsetof(forever_module::BotSnapshot, ammo_count) == 652);
    CHECK(offsetof(forever_module::BotSnapshot, food_count) == 656);
    CHECK(offsetof(forever_module::BotSnapshot, drink_count) == 660);
    CHECK(offsetof(forever_module::BotSnapshot, ammo_vendor_valid) == 664);
    CHECK(offsetof(forever_module::BotSnapshot, ammo_vendor_x) == 668);
    CHECK(offsetof(forever_module::BotSnapshot, food_vendor_x) == 680);
    CHECK(offsetof(forever_module::BotSnapshot, ammo_vendor_guid) == 696);
    CHECK(offsetof(forever_module::BotSnapshot, food_vendor_guid) == 704);
    CHECK(offsetof(forever_module::NearbyUnit, aura_count) == 40);
    CHECK(offsetof(forever_module::NearbyUnit, aura_mask) == 44);

    std::string const name = "trinity_module_test_" + std::to_string(::getpid()) + "_tier2";

    std::string error;
    ModuleTier2Region::Bounds bounds;
    bounds.MaxBots = 4;
    bounds.MaxNearbyUnits = 2;
    bounds.MaxActions = 8;
    bounds.TickInterval = 3;
    auto region = ModuleTier2Region::Create(name, bounds, error);
    CHECK(region != nullptr);
    if (!region)
    {
        std::printf("create failed: %s\n", error.c_str());
        return 1;
    }

    // Cadence: every third tick.
    CHECK(region->SnapshotDue(3));
    CHECK(region->SnapshotDue(6));
    CHECK(!region->SnapshotDue(4));

    // A module attaches by name and sees the layout version and bounds the core granted.
    bi::shared_memory_object shm(bi::open_only, name.c_str(), bi::read_only);
    bi::mapped_region map(shm, bi::read_only);
    auto const* header = static_cast<forever_module::Tier2RegionHeader const*>(map.get_address());
    CHECK(header->layout_version == forever_module::kTier2LayoutVersion);
    CHECK(header->max_bots == 4);
    CHECK(header->max_nearby_units == 2);
    CHECK(header->max_actions == 8);
    CHECK(header->tick_interval == 3);
    CHECK(header->published_snapshot_tick.load() == 0);

    // Publish two snapshots: the module must see the second one, and the buffer index must flip.
    region->PublishSnapshot(3);
    CHECK(header->published_snapshot_tick.load() == 3);
    uint8_t const firstBuffer = header->published_snapshot_buffer.load();
    region->PublishSnapshot(6);
    CHECK(header->published_snapshot_tick.load() == 6);
    CHECK(header->published_snapshot_buffer.load() != firstBuffer);
    CHECK(region->PublishedSnapshotTick() == 6);

    // The published buffer holds the header written for tick 6.
    auto const* base = static_cast<std::byte const*>(map.get_address());
    auto const* snapshot = reinterpret_cast<forever_module::WorldTickSnapshotHeader const*>(
        base + forever_module::Tier2SnapshotBufferOffset(4, 2, header->published_snapshot_buffer.load()));
    CHECK(snapshot->tick_number == 6);

    // Actions: the module writes into the buffer the core is not reading, then publishes the tick and the index,
    // the way the SDK's Tier2Session::PublishActions does. The core reads only what is new.
    bi::shared_memory_object shmRw(bi::open_only, name.c_str(), bi::read_write);
    bi::mapped_region rw(shmRw, bi::read_write);
    auto* rwBase = static_cast<std::byte*>(rw.get_address());
    auto* rwHeader = static_cast<forever_module::Tier2RegionHeader*>(rw.get_address());

    auto publishActions = [&](std::uint64_t actionTick, std::vector<forever_module::BotAction> const& actions,
                              std::uint32_t claimedCount)
    {
        std::uint8_t const target = rwHeader->published_action_buffer.load() ^ 1;
        auto* queue = reinterpret_cast<forever_module::ActionQueueHeader*>(
            rwBase + forever_module::Tier2ActionBufferOffset(4, 2, 8, target));
        queue->tick_number = actionTick;
        queue->action_count = claimedCount;
        if (!actions.empty())
            std::memcpy(static_cast<void*>(queue + 1), actions.data(), actions.size() * sizeof(forever_module::BotAction));
        rwHeader->published_action_tick.store(actionTick);
        rwHeader->published_action_buffer.store(target);
    };

    forever_module::BotAction move;
    move.bot_guid = 42;
    move.type = forever_module::ActionType::kMoveTo;
    move.dest_x = 1.0f;
    forever_module::BotAction stop;
    stop.bot_guid = 42;
    stop.type = forever_module::ActionType::kStop;

    ModuleActionSet actions;
    CHECK(!region->ReadNewActions(actions)); // nothing published yet

    publishActions(7, {move, stop}, 2);
    CHECK(region->ReadNewActions(actions));
    CHECK(actions.Tick == 7);
    CHECK(actions.Actions.size() == 2);
    if (actions.Actions.size() == 2)
    {
        CHECK(actions.Actions[0].bot_guid == 42 && actions.Actions[0].type == forever_module::ActionType::kMoveTo);
        CHECK(actions.Actions[0].dest_x == 1.0f);
        CHECK(actions.Actions[1].type == forever_module::ActionType::kStop);
    }
    CHECK(!region->ReadNewActions(actions)); // the same set is not returned twice

    // An empty set is still new (it tells the core the module now wants nothing).
    publishActions(8, {}, 0);
    CHECK(region->ReadNewActions(actions));
    CHECK(actions.Tick == 8);

    // The layout-11 death actions round-trip.
    forever_module::BotAction release; release.bot_guid = 42; release.type = forever_module::ActionType::kReleaseSpirit;
    forever_module::BotAction revive; revive.bot_guid = 42; revive.type = forever_module::ActionType::kResurrect;
    revive.target_guid = forever_module::MakeUnitRef(2, 0, 7);
    forever_module::BotAction reclaim; reclaim.bot_guid = 42; reclaim.type = forever_module::ActionType::kReclaimCorpse;
    publishActions(9, {release, revive, reclaim}, 3);
    CHECK(region->ReadNewActions(actions));
    CHECK(actions.Actions.size() == 3);
    if (actions.Actions.size() == 3)
    {
        CHECK(actions.Actions[0].type == forever_module::ActionType::kReleaseSpirit);
        CHECK(actions.Actions[1].type == forever_module::ActionType::kResurrect && actions.Actions[1].target_guid == forever_module::MakeUnitRef(2, 0, 7));
        CHECK(actions.Actions[2].type == forever_module::ActionType::kReclaimCorpse);
    }
    // The same set is not returned twice, and reading it again leaves the last one in place. (This line used to read
    // CHECK(actions.Actions.empty()) here, which failed on HEAD: it asserted the set empty immediately after checking it
    // had three actions, and no read happened in between. See git log for the fix.)
    CHECK(!region->ReadNewActions(actions));
    CHECK(actions.Actions.size() == 3 && actions.Actions[0].type == forever_module::ActionType::kReleaseSpirit);

    // The layout-12 and layout-13 action types round-trip as well. The core refuses kCraft and kGather with a reason
    // (not implemented), but they are inside the layout, so the region has to carry them unchanged.
    forever_module::BotAction craft; craft.bot_guid = 42; craft.type = forever_module::ActionType::kCraft; craft.spell_id = 51005;
    forever_module::BotAction gather; gather.bot_guid = 42; gather.type = forever_module::ActionType::kGather;
    gather.target_guid = forever_module::MakeUnitRef(8, 1731, 3);
    forever_module::BotAction petGo; petGo.bot_guid = 42; petGo.type = forever_module::ActionType::kPetCommand;
    petGo.spell_id = 1; petGo.target_guid = forever_module::MakeUnitRef(8, 1234, 7);
    publishActions(10, {craft, gather, petGo}, 3);
    CHECK(region->ReadNewActions(actions));
    CHECK(actions.Actions.size() == 3);
    if (actions.Actions.size() == 3)
    {
        CHECK(actions.Actions[0].type == forever_module::ActionType::kCraft && actions.Actions[0].spell_id == 51005);
        CHECK(actions.Actions[1].type == forever_module::ActionType::kGather &&
              actions.Actions[1].target_guid == forever_module::MakeUnitRef(8, 1731, 3));
        CHECK(actions.Actions[2].type == forever_module::ActionType::kPetCommand &&
              actions.Actions[2].target_guid == forever_module::MakeUnitRef(8, 1234, 7));
    }

    // A claimed count above MaxActions is clamped to what the region holds (8 here).
    std::vector<forever_module::BotAction> full(8, stop);
    publishActions(9, full, 9);
    CHECK(region->ReadNewActions(actions));
    CHECK(actions.Actions.size() == 8);

    // Snapshot contents: a bot with one nearby unit, then five bots against MaxBots = 4 (the fifth is dropped).
    forever_module::BotSnapshot botEntry;
    botEntry.guid = 42;
    botEntry.health = 500;
    botEntry.max_health = 1000;
    forever_module::NearbyUnit nearbyEntry;
    nearbyEntry.guid = forever_module::MakeUnitRef(8, 1234, 7);
    nearbyEntry.is_hostile = true;
    std::vector<ModuleSnapshotBot> withBot(1);
    withBot[0].Bot = botEntry;
    withBot[0].Nearby = { nearbyEntry };
    region->PublishSnapshot(9, withBot);

    uint8_t const contentBuffer = header->published_snapshot_buffer.load();
    auto const* contentSnap = reinterpret_cast<forever_module::WorldTickSnapshotHeader const*>(
        base + forever_module::Tier2SnapshotBufferOffset(4, 2, contentBuffer));
    CHECK(contentSnap->tick_number == 9);
    CHECK(contentSnap->bot_count == 1);
    auto const* botSlots = reinterpret_cast<forever_module::BotSnapshot const*>(contentSnap + 1);
    CHECK(botSlots[0].guid == 42);
    CHECK(botSlots[0].health == 500);
    CHECK(botSlots[0].nearby_count == 1);
    auto const* nearbySlots = reinterpret_cast<forever_module::NearbyUnit const*>(botSlots + 4);
    CHECK(nearbySlots[0].guid == forever_module::MakeUnitRef(8, 1234, 7));
    CHECK(nearbySlots[0].is_hostile);

    // The layout-13 fields survive the trip through the region as well: a module reads the auras, the pet, the NPC
    // references, the money and the reason the bot lost its target out of exactly these bytes.
    withBot[0].Bot.aura_count = 2;
    withBot[0].Bot.auras[0] = { 1459 /* Bloodthirst */, 6000, 1 };
    withBot[0].Bot.auras[1] = { 9747, 0xFFFFFFFFu, 1 };          // an aura with no duration: the core's sentinel
    withBot[0].Bot.auras[2] = { 0, 0, 0 };                      // past aura_count: an unused slot reads as spell_id 0
    withBot[0].Bot.casting_target = forever_module::MakeUnitRef(8, 1234, 7);
    withBot[0].Bot.pet_guid = forever_module::MakeUnitRef(10, 332, 9);
    withBot[0].Bot.pet_health = 120;
    withBot[0].Bot.pet_max_health = 400;
    withBot[0].Bot.pet_state = 1;
    withBot[0].Bot.vendor_guid = forever_module::MakeUnitRef(8, 4277, 21);
    withBot[0].Bot.repair_guid = forever_module::MakeUnitRef(8, 4277, 22);
    withBot[0].Bot.auctioneer_guid = forever_module::MakeUnitRef(8, 4277, 23);
    withBot[0].Bot.copper = 123456;
    withBot[0].Bot.bag_free_slots = 9;
    withBot[0].Bot.last_target_lost_reason = 2;
    // Layout 15: what the bot carries and the vendors that sell what it can use.
    withBot[0].Bot.ammo_entry = 2512;
    withBot[0].Bot.ammo_count = 87;
    withBot[0].Bot.food_count = 14;
    withBot[0].Bot.drink_count = 6;
    withBot[0].Bot.ammo_vendor_valid = 1;
    withBot[0].Bot.ammo_vendor_x = 10.5f;
    withBot[0].Bot.ammo_vendor_y = -20.25f;
    withBot[0].Bot.ammo_vendor_z = 3.0f;
    withBot[0].Bot.ammo_vendor_guid = forever_module::MakeUnitRef(8, 4553, 31);
    withBot[0].Bot.food_vendor_valid = 1;
    withBot[0].Bot.food_vendor_x = -7.0f;
    withBot[0].Bot.food_vendor_y = 8.0f;
    withBot[0].Bot.food_vendor_z = 9.5f;
    withBot[0].Bot.food_vendor_guid = forever_module::MakeUnitRef(8, 1465, 12);
    // Layout 18: professions.
    withBot[0].Bot.profession_skill[0] = 186;
    withBot[0].Bot.profession_skill[1] = 202;
    withBot[0].Bot.profession_value[0] = 75;
    withBot[0].Bot.profession_max[0] = 150;
    withBot[0].Bot.profession_value[1] = 1;
    withBot[0].Bot.profession_max[1] = 75;
    // Layout 17: the flight master and the flight.
    withBot[0].Bot.taxi_master_valid = 1;
    withBot[0].Bot.in_flight = 1;
    withBot[0].Bot.taxi_master_x = 11.5f;
    withBot[0].Bot.taxi_master_y = -12.5f;
    withBot[0].Bot.taxi_master_z = 13.5f;
    withBot[0].Bot.taxi_master_guid = forever_module::MakeUnitRef(8, 3838, 5);
    // Layout 16: potions, bandages and the vendor that sells them.
    withBot[0].Bot.health_potion_count = 4;
    withBot[0].Bot.mana_potion_count = 2;
    withBot[0].Bot.bandage_count = 11;
    withBot[0].Bot.goods_vendor_valid = 1;
    withBot[0].Bot.goods_vendor_x = 1.5f;
    withBot[0].Bot.goods_vendor_y = -2.5f;
    withBot[0].Bot.goods_vendor_z = 3.5f;
    withBot[0].Bot.goods_vendor_guid = forever_module::MakeUnitRef(8, 5100, 41);
    withBot[0].Nearby[0].aura_count = 3;
    withBot[0].Nearby[0].aura_mask = 0x00000005; // a DOT and a poison (see doc/module-system/20)
    region->PublishSnapshot(11, withBot);

    uint8_t const layout13Buffer = header->published_snapshot_buffer.load();
    auto const* layout13Snap = reinterpret_cast<forever_module::WorldTickSnapshotHeader const*>(
        base + forever_module::Tier2SnapshotBufferOffset(4, 2, layout13Buffer));
    CHECK(layout13Snap->tick_number == 11);
    auto const* read13 = reinterpret_cast<forever_module::BotSnapshot const*>(layout13Snap + 1);
    CHECK(read13->aura_count == 2);
    CHECK(read13->auras[0].spell_id == 1459 && read13->auras[0].remaining_ms == 6000 && read13->auras[0].stacks == 1);
    CHECK(read13->auras[1].spell_id == 9747 && read13->auras[1].remaining_ms == 0xFFFFFFFFu);
    CHECK(read13->auras[2].spell_id == 0);
    CHECK(read13->casting_target == forever_module::MakeUnitRef(8, 1234, 7));
    CHECK(read13->pet_guid == forever_module::MakeUnitRef(10, 332, 9));
    CHECK(read13->pet_state == 1 && read13->pet_health == 120 && read13->pet_max_health == 400);
    CHECK(read13->vendor_guid == forever_module::MakeUnitRef(8, 4277, 21));
    CHECK(read13->repair_guid == forever_module::MakeUnitRef(8, 4277, 22));
    CHECK(read13->auctioneer_guid == forever_module::MakeUnitRef(8, 4277, 23));
    CHECK(read13->copper == 123456 && read13->bag_free_slots == 9);
    CHECK(read13->last_target_lost_reason == 2);
    CHECK(read13->ammo_entry == 2512 && read13->ammo_count == 87 && read13->food_count == 14 && read13->drink_count == 6);
    CHECK(read13->ammo_vendor_valid == 1 && read13->ammo_vendor_x == 10.5f && read13->ammo_vendor_y == -20.25f && read13->ammo_vendor_z == 3.0f);
    CHECK(read13->ammo_vendor_guid == forever_module::MakeUnitRef(8, 4553, 31));
    CHECK(read13->food_vendor_valid == 1 && read13->food_vendor_x == -7.0f && read13->food_vendor_y == 8.0f && read13->food_vendor_z == 9.5f);
    CHECK(read13->food_vendor_guid == forever_module::MakeUnitRef(8, 1465, 12));
    CHECK(read13->health_potion_count == 4 && read13->mana_potion_count == 2 && read13->bandage_count == 11);
    CHECK(read13->goods_vendor_valid == 1 && read13->goods_vendor_x == 1.5f && read13->goods_vendor_y == -2.5f && read13->goods_vendor_z == 3.5f);
    CHECK(read13->goods_vendor_guid == forever_module::MakeUnitRef(8, 5100, 41));
    CHECK(read13->taxi_master_valid == 1 && read13->in_flight == 1 && read13->taxi_master_x == 11.5f && read13->taxi_master_y == -12.5f && read13->taxi_master_z == 13.5f);
    CHECK(read13->taxi_master_guid == forever_module::MakeUnitRef(8, 3838, 5));
    CHECK(read13->profession_skill[0] == 186 && read13->profession_skill[1] == 202 && read13->profession_value[0] == 75 && read13->profession_max[0] == 150);
    CHECK(read13->profession_value[1] == 1 && read13->profession_max[1] == 75);
    auto const* read13Near = reinterpret_cast<forever_module::NearbyUnit const*>(read13 + 4);
    CHECK(read13Near[0].aura_count == 3 && read13Near[0].aura_mask == 0x00000005);

    // And back to zero: an entry written from scratch must not keep the previous tick's facts.
    withBot[0].Bot = forever_module::BotSnapshot();
    withBot[0].Bot.guid = 42;
    withBot[0].Nearby[0] = forever_module::NearbyUnit();
    withBot[0].Nearby[0].guid = forever_module::MakeUnitRef(8, 1234, 7);
    region->PublishSnapshot(12, withBot);
    uint8_t const zeroBuffer = header->published_snapshot_buffer.load();
    auto const* zeroSnap = reinterpret_cast<forever_module::WorldTickSnapshotHeader const*>(
        base + forever_module::Tier2SnapshotBufferOffset(4, 2, zeroBuffer));
    auto const* readZero = reinterpret_cast<forever_module::BotSnapshot const*>(zeroSnap + 1);
    CHECK(readZero->aura_count == 0);
    CHECK(readZero->pet_state == 0);
    CHECK(readZero->copper == 0);
    CHECK(readZero->last_target_lost_reason == 0);

    std::vector<ModuleSnapshotBot> tooMany(5, withBot[0]);
    region->PublishSnapshot(13, tooMany);
    uint8_t const clampBuffer = header->published_snapshot_buffer.load();
    auto const* clampSnap = reinterpret_cast<forever_module::WorldTickSnapshotHeader const*>(
        base + forever_module::Tier2SnapshotBufferOffset(4, 2, clampBuffer));
    CHECK(clampSnap->bot_count == 4);

    // Removing the region drops the name, so a later module attach fails.
    region.reset();
    bool gone = false;
    try
    {
        bi::shared_memory_object again(bi::open_only, name.c_str(), bi::read_only);
    }
    catch (bi::interprocess_exception const&)
    {
        gone = true;
    }
    CHECK(gone);

    if (g_failures == 0)
    {
        std::printf("PASS  Tier 2 region\n");
        return 0;
    }
    std::printf("%d check(s) failed\n", g_failures);
    return 1;
}

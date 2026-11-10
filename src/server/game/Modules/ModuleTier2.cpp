/*
 * Tier 2 shared-memory region. See ModuleTier2.h.
 */

#include "ModuleTier2.h"
#include "Log.h"

#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/shared_memory_object.hpp>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <new>

using namespace forever_module;

// ABI pins for the structs shared with the module through shared memory. The SDK pins the same numbers (src/tier2_session.cpp); a field added or
// reordered on one side only changes a size here or there and stops the build instead of corrupting the region at run time. If one of these fires
// on purpose: bump kTier2LayoutVersion and kProtocolVersion in both repositories and update this list together with the SDK's.
static_assert(sizeof(BotSnapshot) == 1856, "BotSnapshot changed size: bump kTier2LayoutVersion and update this check (and the SDK's)");
static_assert(sizeof(NearbyUnit) == 136, "NearbyUnit changed size: bump kTier2LayoutVersion and update this check (and the SDK's)");
static_assert(sizeof(GroundEffect) == 24, "GroundEffect changed size: bump kTier2LayoutVersion and update this check (and the SDK's)");
static_assert(sizeof(Telegraph) == 48, "Telegraph changed size: bump kTier2LayoutVersion and update this check (and the SDK's)");
static_assert(sizeof(ProbeResult) == 32, "ProbeResult changed size: bump kTier2LayoutVersion and update this check (and the SDK's)");
static_assert(sizeof(PartyMember) == 176, "PartyMember changed size: bump kTier2LayoutVersion and update this check (and the SDK's)");
static_assert(sizeof(PartyView) == 320, "PartyView changed size: bump kTier2LayoutVersion and update this check (and the SDK's)");
static_assert(sizeof(BotAction) == 40, "BotAction changed size: bump kTier2LayoutVersion and update this check (and the SDK's)");
static_assert(sizeof(Tier2RegionHeader) == 304, "Tier2RegionHeader changed size: bump kTier2LayoutVersion and update this check (and the SDK's)");
static_assert(sizeof(WorldTickSnapshotHeader) == 24, "WorldTickSnapshotHeader changed size: bump kTier2LayoutVersion and update this check (and the SDK's)");
static_assert(sizeof(ActionQueueHeader) == 16, "ActionQueueHeader changed size: bump kTier2LayoutVersion and update this check (and the SDK's)");
static_assert(offsetof(BotSnapshot, party_view_index) == 1508 && offsetof(BotSnapshot, telegraphs) == 1528 && offsetof(BotSnapshot, probes) == 1720 &&
              offsetof(NearbyUnit, aggro_radius) == 128 && offsetof(Tier2RegionHeader, sections) == 112, "layout 25 offsets (and the SDK's)");

ModuleTier2Region::ModuleTier2Region(std::string name, Bounds bounds)
    : _name(std::move(name)), _bounds(bounds)
{
}

ModuleTier2Region::~ModuleTier2Region()
{
    // Unmap before removing the name so the region can't be reopened by a stale module handle.
    _mapping.reset();
    _shm.reset();
    if (!_name.empty())
        boost::interprocess::shared_memory_object::remove(_name.c_str());
}

std::shared_ptr<ModuleTier2Region> ModuleTier2Region::Create(std::string const& name, Bounds bounds, std::string& error)
{
    using namespace boost::interprocess;

    // Layout 25: resolve the party slots once; the header carries them, so a module needs no formula.
    bounds.MaxPartyViews = bounds.MaxPartyViews == 0 ? Tier2PartyViewSlots(bounds.MaxBots)
                                                     : std::min<uint32>(bounds.MaxPartyViews, std::max<uint32>(bounds.MaxBots, 1));
    uint32 const partyMembers = Tier2PartyMemberSlots(bounds.MaxBots, bounds.MaxPartyViews);
    std::size_t const total = Tier2RegionBytes(bounds.MaxBots, bounds.MaxNearbyUnits, bounds.MaxActions, bounds.MaxPartyViews, partyMembers);

    std::shared_ptr<ModuleTier2Region> region(new ModuleTier2Region(name, bounds));
    region->_snapshotOffset = Tier2HeaderBytes();
    region->_snapshotBytes = Tier2SnapshotBufferBytes(bounds.MaxBots, bounds.MaxNearbyUnits, bounds.MaxPartyViews, partyMembers);
    region->_actionOffset = Tier2ActionBufferOffset(bounds.MaxBots, bounds.MaxNearbyUnits, bounds.MaxActions, bounds.MaxPartyViews, partyMembers, 0);
    region->_actionBytes = Tier2ActionBufferBytes(bounds.MaxActions);

    try
    {
        // A region left behind by a crashed core would make create_only fail.
        shared_memory_object::remove(name.c_str());
        region->_shm = std::make_unique<shared_memory_object>(create_only, name.c_str(), read_write);
        region->_shm->truncate(static_cast<offset_t>(total));
        region->_mapping = std::make_unique<mapped_region>(*region->_shm, read_write);
    }
    catch (std::exception const& e)
    {
        error = "cannot create shared memory '" + name + "': " + e.what();
        return nullptr;
    }

    void* base = region->_mapping->get_address();
    region->_header = new (base) Tier2RegionHeader();
    region->_header->layout_version = kTier2LayoutVersion;
    region->_header->max_bots = bounds.MaxBots;
    region->_header->max_nearby_units = bounds.MaxNearbyUnits;
    region->_header->max_actions = bounds.MaxActions;
    region->_header->tick_interval = bounds.TickInterval;
    region->_header->max_party_views = bounds.MaxPartyViews;
    // Layout 25: the geometry and the section table, from the bounds just set (the member pool follows the view count).
    Tier2DescribeRegion(*region->_header);
    return region;
}

bool ModuleTier2Region::SnapshotDue(uint64 tick) const
{
    uint32 const interval = _bounds.TickInterval == 0 ? 1 : _bounds.TickInterval;
    return tick % interval == 0;
}

void ModuleTier2Region::PublishSnapshot(uint64 tick, std::vector<ModuleSnapshotBot> const& bots, ModuleSnapshotParties const& parties)
{
    // Write into the buffer the module is not reading, then publish it: tick first, then the index.
    // The index is module-writable. Anything other than 0 or 1 would walk off the mapping.
    uint8 const published = _header->published_snapshot_buffer.load(std::memory_order_relaxed);
    if (published > 1)
    {
        TC_LOG_ERROR("modules", "Tier 2 region '{}': published snapshot buffer {} is not 0 or 1; skipping publish", _name, published);
        return;
    }
    uint8 const next = static_cast<uint8>(1 - published);
    auto* base = static_cast<std::byte*>(_mapping->get_address());
    std::byte* buffer = base + _snapshotOffset + next * _snapshotBytes;

    uint32 const maxBots = _bounds.MaxBots;
    uint32 const maxNearby = _bounds.MaxNearbyUnits;
    uint32 const count = static_cast<uint32>(std::min<std::size_t>(bots.size(), maxBots));
    uint32 const viewCount = static_cast<uint32>(std::min<std::size_t>(parties.Views.size(), MaxPartyViews()));
    uint32 const memberCount = static_cast<uint32>(std::min<std::size_t>(parties.Members.size(), MaxPartyMembers()));

    auto* snapshot = new (buffer) WorldTickSnapshotHeader();
    snapshot->tick_number = tick;
    snapshot->bot_count = count;
    snapshot->party_view_count = viewCount;
    snapshot->party_member_count = memberCount;

    // Bot slots first, then maxNearby nearby slots per bot slot, then the party views and their member pool (the layout in ModuleTier2Layout.h).
    auto* botSlots = reinterpret_cast<BotSnapshot*>(buffer + Tier2BotsSectionOffset());
    auto* nearbySlots = reinterpret_cast<NearbyUnit*>(buffer + Tier2NearbySectionOffset(maxBots));
    for (uint32 i = 0; i < count; ++i)
    {
        BotSnapshot entry = bots[i].Bot;
        uint32 const nearby = static_cast<uint32>(std::min<std::size_t>(bots[i].Nearby.size(), maxNearby));
        entry.nearby_count = nearby;
        std::memcpy(static_cast<void*>(botSlots + i), &entry, sizeof(entry));
        if (nearby > 0)
            std::memcpy(static_cast<void*>(nearbySlots + std::size_t(i) * maxNearby), bots[i].Nearby.data(),
                        nearby * sizeof(NearbyUnit));
    }
    if (viewCount > 0)
        std::memcpy(static_cast<void*>(buffer + Tier2PartyViewsSectionOffset(maxBots, maxNearby)), parties.Views.data(), viewCount * sizeof(PartyView));
    if (memberCount > 0)
        std::memcpy(static_cast<void*>(buffer + Tier2PartyMembersSectionOffset(maxBots, maxNearby, MaxPartyViews())), parties.Members.data(),
                    memberCount * sizeof(PartyMember));

    _header->published_snapshot_tick.store(tick, std::memory_order_release);
    _header->published_snapshot_buffer.store(next, std::memory_order_release);
}

uint64 ModuleTier2Region::PublishedSnapshotTick() const
{
    return _header->published_snapshot_tick.load(std::memory_order_acquire);
}

uint64 ModuleTier2Region::PublishedActionTick() const
{
    return _header->published_action_tick.load(std::memory_order_acquire);
}

bool ModuleTier2Region::ReadNewActions(ModuleActionSet& out)
{
    auto const* base = static_cast<std::byte const*>(_mapping->get_address());

    for (int attempt = 0; attempt < 3; ++attempt)
    {
        uint8 const buffer = _header->published_action_buffer.load(std::memory_order_acquire);
        // The index is module-writable. Anything other than 0 or 1 would walk off the mapping.
        if (buffer > 1)
        {
            TC_LOG_ERROR("modules", "Tier 2 region '{}': published action buffer {} is not 0 or 1; skipping read", _name, buffer);
            return false;
        }
        uint64 const tick = _header->published_action_tick.load(std::memory_order_acquire);
        // The header tick is the cheap check for "nothing new", so an idle module costs one load per tick.
        if (buffer == _seenActionBuffer && tick == _seenActionTick)
            return false;

        auto const* queue = reinterpret_cast<ActionQueueHeader const*>(base + _actionOffset + buffer * _actionBytes);
        uint32 const count = std::min(queue->action_count, _bounds.MaxActions);
        ModuleActionSet copy;
        copy.Tick = queue->tick_number;
        copy.Actions.resize(count);
        if (count > 0)
            std::memcpy(copy.Actions.data(), reinterpret_cast<std::byte const*>(queue + 1), count * sizeof(forever_module::BotAction));

        // published_action_tick is the sequence. The module stores it after the buffer contents, so a
        // change here means the copy raced a publish, including one that flipped away and back to the same index.
        if (_header->published_action_tick.load(std::memory_order_acquire) != tick ||
            _header->published_action_buffer.load(std::memory_order_acquire) != buffer)
            continue;

        _seenActionBuffer = buffer;
        _seenActionTick = copy.Tick;
        out = std::move(copy);
        return true;
    }
    return false; // the index kept flipping; the next tick tries again
}

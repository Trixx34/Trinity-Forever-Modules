/*
 * Pure rules for filling the shared-memory BotSnapshot (ModuleBotSnapshot.cpp): clamping, capping, sorting and
 * packing on plain integers, with no Player/Unit/Aura dependency so they run in the Catch2 `tests` binary.
 * Every helper here was lifted from ModuleBotSnapshot.cpp with the same values and semantics.
 */

#ifndef TRINITYCORE_MODULE_BOT_SNAPSHOT_RULES_H
#define TRINITYCORE_MODULE_BOT_SNAPSHOT_RULES_H

#include "Define.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <unordered_map>
#include <vector>

namespace ModuleBotSnapshotRules
{
// A value wider than the 32-bit snapshot field saturates at its maximum; a negative signed value is 0.
inline uint32 ClampToUint32(uint64 value)
{
    return static_cast<uint32>(std::min<uint64>(value, std::numeric_limits<uint32>::max()));
}

inline uint32 ClampToUint32(int64 value)
{
    return value <= 0 ? 0 : ClampToUint32(static_cast<uint64>(value));
}

// A remaining cooldown (positive, in ms) as the snapshot's 32-bit field. The caller has already skipped values <= 0.
inline uint32 ClampCooldownRemaining(int64 remaining)
{
    return uint32(std::min<int64>(remaining, std::numeric_limits<uint32>::max()));
}

struct ActiveCooldown
{
    uint32 SpellId;
    uint32 Remaining;
};

// Closest to expiring first (std::sort, as before: equal remainders may come out in either order).
inline void SortCooldowns(std::vector<ActiveCooldown>& active)
{
    std::sort(active.begin(), active.end(), [](ActiveCooldown const& a, ActiveCooldown const& b) { return a.Remaining < b.Remaining; });
}

// How many of `available` entries fit in `capacity` snapshot slots.
inline std::size_t CapCount(std::size_t available, std::size_t capacity)
{
    return std::min(available, capacity);
}

// Damage-hit ring (layout 22). `sequence` counts the hits recorded so far, `lifeStart` is the sequence the current life
// started at, `next` is the slot the next hit goes into. The ring holds `capacity` hits, oldest overwritten first.
inline uint8 DamageHitCount(uint32 sequence, uint32 lifeStart, std::size_t capacity)
{
    return uint8(std::min<uint32>(sequence - lifeStart, uint32(capacity)));
}

// The ring slot of the i-th reported hit, oldest first (i in [0, count)).
inline std::size_t DamageRingSlot(std::size_t next, std::size_t capacity, std::size_t count, std::size_t i)
{
    return (next + capacity - count + i) % capacity;
}

// The slot after `next`.
inline std::size_t NextDamageSlot(std::size_t next, std::size_t capacity)
{
    return (next + 1) % capacity;
}

// Sorts the nearest `maxNearby` units to the front (the rest in unspecified order); a full sort when all fit.
template <typename T, typename Less>
inline void SortNearestPrefix(std::vector<T>& units, std::size_t maxNearby, Less less)
{
    std::size_t const prefix = std::min<std::size_t>(units.size(), maxNearby);
    if (prefix < units.size())
        std::partial_sort(units.begin(), units.begin() + prefix, units.end(), less);
    else
        std::sort(units.begin(), units.end(), less);
}

// Picks the nearby units of one snapshot from the three category lists, each already closest first: the closest
// `playerSlotsWanted` players, then creatures, then corpses, then any players left over while there is room; then the
// result is sorted by `less`. `chosen` is expected empty.
template <typename T, typename Less>
inline void ChooseNearby(std::vector<T> const& players, std::vector<T> const& creatures, std::vector<T> const& corpses,
    std::vector<T>& chosen, std::size_t maxNearby, std::size_t playerSlotsWanted, Less less)
{
    std::size_t const playerSlots = std::min<std::size_t>(playerSlotsWanted, maxNearby);
    for (T const& player : players)
    {
        if (chosen.size() >= playerSlots)
            break;
        chosen.push_back(player);
    }
    std::size_t const leadingPlayers = chosen.size();
    for (T const& creature : creatures)
    {
        if (chosen.size() >= maxNearby)
            break;
        chosen.push_back(creature);
    }
    for (T const& corpse : corpses)
    {
        if (chosen.size() >= maxNearby)
            break;
        chosen.push_back(corpse);
    }
    for (std::size_t i = leadingPlayers; i < players.size() && chosen.size() < maxNearby; ++i)
        chosen.push_back(players[i]);
    std::sort(chosen.begin(), chosen.end(), less);
}

// AuraEntry::remaining_ms for an aura with no duration.
inline constexpr uint32 kInfiniteAuraMs = std::numeric_limits<uint32>::max();

inline uint32 AuraRemainingMs(bool permanent, int32 left)
{
    return permanent ? kInfiniteAuraMs : uint32(left);
}

inline uint32 AuraStacks(uint32 stackAmount)
{
    return std::max<uint32>(1, stackAmount);
}

// NearbyUnit::aura_mask bits, frozen with layout 13: never move or repurpose one.
enum AuraMaskBit : uint32
{
    kAuraMaskPeriodicDamage = 0x00000001,
    kAuraMaskPeriodicHeal   = 0x00000002,
    kAuraMaskPoison         = 0x00000004,
    kAuraMaskCrowdControl   = 0x00000008,
    kAuraMaskRoot           = 0x00000010,
    kAuraMaskSlow           = 0x00000020,
    kAuraMaskInvisibility   = 0x00000040,
    kAuraMaskAbsorb         = 0x00000080,
};

// What a spell's effects say, read from SpellInfo by the caller.
struct AuraTraits
{
    bool PeriodicDamage = false;
    bool PeriodicHeal = false;
    bool PoisonOrDisease = false;   // Dispel is DISPEL_POISON or DISPEL_DISEASE
    bool Stun = false;
    bool Fear = false;
    bool Confuse = false;
    bool RootOrSnareMechanic = false;
    bool Slow = false;
    bool Invisibility = false;
    bool SchoolAbsorb = false;
    bool DamageShield = false;
};

inline uint32 BuildAuraMask(AuraTraits const& t)
{
    uint32 mask = 0;
    if (t.PeriodicDamage)
        mask |= kAuraMaskPeriodicDamage;
    if (t.PeriodicHeal)
        mask |= kAuraMaskPeriodicHeal;
    if (t.PoisonOrDisease)
        mask |= kAuraMaskPoison;
    if (t.Stun || t.Fear || t.Confuse)
        mask |= kAuraMaskCrowdControl;
    if (t.RootOrSnareMechanic)
        mask |= kAuraMaskRoot;
    if (t.Slow)
        mask |= kAuraMaskSlow;
    if (t.Invisibility)
        mask |= kAuraMaskInvisibility;
    if (t.SchoolAbsorb || t.DamageShield)
        mask |= kAuraMaskAbsorb;
    return mask;
}

// A lazily filled cache of a pure per-spell value, keyed by spell id. `tag` identifies the static data the value was worked
// out from (the SpellInfo pointer): an entry whose tag differs is worked out again and replaced, so a reloaded spell store
// can never serve a stale value. Not thread-safe: use one per thread (the snapshot builder keeps a thread_local one).
template <typename Value>
class SpellIdCache
{
public:
    template <typename Compute>
    Value Get(uint32 id, void const* tag, Compute&& compute)
    {
        auto found = _entries.find(id);
        if (found != _entries.end() && found->second.Tag == tag)
            return found->second.Result;
        Value const result = compute();
        _entries[id] = Entry{ tag, result };
        return result;
    }

    std::size_t Size() const { return _entries.size(); }

private:
    struct Entry
    {
        void const* Tag;
        Value Result;
    };
    std::unordered_map<uint32, Entry> _entries;
};
}

#endif // TRINITYCORE_MODULE_BOT_SNAPSHOT_RULES_H

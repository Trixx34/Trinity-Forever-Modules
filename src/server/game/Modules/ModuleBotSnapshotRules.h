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

// Layout 24: puts the engaged units ahead of the rest, so that a pack fighting the bot's group is never crowded out of the list by sleeping mobs.
// Each part is ordered nearest first up to `limit` units, and the engaged part comes first. `units` is rewritten in place.
template <typename T, typename Less, typename Engaged>
inline void EngagedFirst(std::vector<T>& units, std::size_t limit, Less less, Engaged engaged)
{
    auto const middle = std::stable_partition(units.begin(), units.end(), engaged);
    auto sortPart = [&](auto first, auto last)
    {
        std::size_t const size = std::size_t(last - first);
        if (size > limit)
            std::partial_sort(first, first + limit, last, less);
        else
            std::sort(first, last, less);
    };
    sortPart(units.begin(), middle);
    sortPart(middle, units.end());
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

// ---------------------------------------------------------------------------------------------------------------
// Layout 25: the party view, enemy telegraphs and position probes (ModuleBotPartyView.cpp, ModuleBotSnapshot.cpp, ModuleBotActions.cpp).
// ---------------------------------------------------------------------------------------------------------------

// PartyMember::role bits (forever_module::PartyRole: tank 1, healer 2, damage 4) from the group's LFG roles (lfg::PLAYER_ROLE_TANK 0x02, HEALER 0x04,
// DAMAGE 0x08), its member flags (MEMBER_FLAG_MAINTANK 0x02 makes a tank) and, when both say nothing, the talent role a bot's module gave it
// (BotMgr::SetTalentRole: 1 tank, 2 healer, 3 dps). A human without LFG roles or flags has no role bits.
inline uint8 PartyRoleBits(uint8 lfgRoles, uint8 memberFlags, uint8 talentRole)
{
    uint8 bits = 0;
    if (lfgRoles & 0x02)
        bits |= 1;
    if (lfgRoles & 0x04)
        bits |= 2;
    if (lfgRoles & 0x08)
        bits |= 4;
    if (memberFlags & 0x02)
        bits |= 1;
    if (bits == 0)
    {
        if (talentRole == 1)
            bits = 1;
        else if (talentRole == 2)
            bits = 2;
        else if (talentRole == 3)
            bits = 4;
    }
    return bits;
}

inline bool IsPartyTank(uint8 roleBits)
{
    return (roleBits & 1) != 0;
}

// A harmful aura on a member, as the view lists it: the dispellable ones first (a module's dispel scan reads the front), then the longest
// remaining first (the one that will hurt longest). Equal keys keep their order.
struct PartyDebuffFact
{
    uint32 SpellId = 0;
    uint32 RemainingMs = 0;
    uint8 DispelType = 0;   // 0 none, 1 magic, 2 curse, 3 disease, 4 poison
    uint8 Stacks = 0;
};

inline void SortPartyDebuffs(std::vector<PartyDebuffFact>& facts)
{
    std::stable_sort(facts.begin(), facts.end(), [](PartyDebuffFact const& a, PartyDebuffFact const& b)
    {
        bool const da = a.DispelType != 0;
        bool const db = b.DispelType != 0;
        if (da != db)
            return da;
        return a.RemainingMs > b.RemainingMs;
    });
}

// A creature on a member who is not a tank (PartyView::loose_mobs): the ones on healers first, then the newest first (a fresh aggro loss is the
// one to answer). StartMs is the game time it was first seen loose, kept between ticks by the watch.
struct LooseMobFact
{
    uint64 UnitRef = 0;
    uint64 VictimRef = 0;
    bool VictimIsHealer = false;
    uint32 SinceMs = 0;
    uint32 StartMs = 0;
};

inline void SortLooseMobs(std::vector<LooseMobFact>& facts)
{
    std::stable_sort(facts.begin(), facts.end(), [](LooseMobFact const& a, LooseMobFact const& b)
    {
        if (a.VictimIsHealer != b.VictimIsHealer)
            return a.VictimIsHealer;
        return a.SinceMs < b.SinceMs;
    });
}

// Whether a group's view is rebuilt on this tick: always while it is active (a member in combat, or the leader in an instance), else one tick in
// idleEvery, staggered by the group id so idle groups spread over the ticks. idleEvery 0 or 1 is every tick.
inline bool PartyViewDue(bool active, uint64 tick, uint64 groupId, uint32 idleEvery)
{
    return active || idleEvery <= 1 || (tick + groupId) % idleEvery == 0;
}

// Telegraph::kind from the shape of a spell in progress: 2 a cone from the caster (any cone target), else 1 a circle at the cast's destination (an
// area target around a destination, when the cast has one), else 3 a circle around the caster (an area target around the caster), else 0 (a cast
// with no area: no telegraph).
inline uint8 TelegraphKind(bool coneTarget, bool areaAtDest, bool areaAtCaster, bool hasDest)
{
    if (coneTarget)
        return 2;
    if (areaAtDest && hasDest)
        return 1;
    if (areaAtCaster)
        return 3;
    return 0;
}

// Telegraphs are listed soonest to land first; a tie goes to the larger area.
struct TelegraphOrderKey
{
    uint32 RemainingMs;
    float Radius;
};

inline bool TelegraphBefore(TelegraphOrderKey const& a, TelegraphOrderKey const& b)
{
    if (a.RemainingMs != b.RemainingMs)
        return a.RemainingMs < b.RemainingMs;
    return a.Radius > b.Radius;
}

// A per-tick budget: at most `limit` takes in one game-time tick (the position probes, Bot.Probe.PerTick). The count starts over when the tick
// changes. limit 0 refuses every take.
struct TickBudget
{
    uint32 Tick = 0;
    uint32 Used = 0;

    bool Take(uint32 nowMs, uint32 limit)
    {
        if (nowMs != Tick)
        {
            Tick = nowMs;
            Used = 0;
        }
        if (Used >= limit)
            return false;
        ++Used;
        return true;
    }
};

// Probe-result ring (layout 25): the same arithmetic as the damage ring, with kMaxProbeResults slots.
inline uint8 ProbeCount(uint32 sequence, std::size_t capacity)
{
    return uint8(std::min<uint32>(sequence, uint32(capacity)));
}
}

#endif // TRINITYCORE_MODULE_BOT_SNAPSHOT_RULES_H

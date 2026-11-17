// Two pure helpers for the kMoveTo route check (no game types, so they are unit tested standalone: tests/ModuleBotNoPathCacheTest.cpp).
//
// 1. NoPathCache: a bounded, shared memory of destinations the navmesh could not reach. A failed search is the most expensive kind (Detour
//    expands the whole reachable region before it gives up, the node pool is 32768), and a bot that explores picks a new unreachable point
//    every few seconds. The key is (map, destination cell, destination height band, start cell). An entry lives for baseTtl and doubles
//    with every repeated failure up to maxTtl, so a point that stays unreachable is asked less and less. An answer is only served to a
//    bot that is not the one that stored it after a second bot has failed on the same key (a bot stuck off the mesh must not close a
//    start cell for every other bot).
//
// 2. PathHandoff: kMoveTo computes a path to decide "reachable?" and then MovePoint computes the same path again when the generator starts
//    (on the next movement update). The checked path is parked per bot and the generator takes it when the unit, the destination and the
//    start position are exactly the same and it is younger than maxAgeMs. Anything else (the bot moved, another destination, too old)
//    makes the generator search as before.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <utility>

namespace ModuleBotNoPathCache
{

struct Key
{
    uint32_t MapId = 0;
    int32_t DestX = 0, DestY = 0, DestZ = 0;
    int32_t StartX = 0, StartY = 0;
    bool operator==(Key const& o) const
    {
        return MapId == o.MapId && DestX == o.DestX && DestY == o.DestY && DestZ == o.DestZ && StartX == o.StartX && StartY == o.StartY;
    }
};

struct KeyHash
{
    std::size_t operator()(Key const& k) const
    {
        uint64_t h = uint64_t(k.MapId) * 0x9E3779B97F4A7C15ull;
        auto mix = [&h](int32_t v) { h ^= uint64_t(uint32_t(v)) + 0x7F4A7C15ull + (h << 6) + (h >> 2); };
        mix(k.DestX); mix(k.DestY); mix(k.DestZ); mix(k.StartX); mix(k.StartY);
        return std::size_t(h);
    }
};

inline int32_t Cell(float coord, float cellYards)
{
    return int32_t(std::floor(coord / (cellYards > 1.0f ? cellYards : 1.0f)));
}

// startCellYards 0 = the start does not take part in the key (every start shares the answer).
inline Key MakeKey(uint32_t mapId, float dx, float dy, float dz, float sx, float sy, float destCellYards, float heightBandYards, float startCellYards)
{
    return Key{ mapId, Cell(dx, destCellYards), Cell(dy, destCellYards), Cell(dz, heightBandYards),
                startCellYards > 0.0f ? Cell(sx, startCellYards) : 0, startCellYards > 0.0f ? Cell(sy, startCellYards) : 0 };
}

class Cache
{
public:
    // baseTtlMs 0 = the cache is off (nothing is stored, nothing is served).
    void Configure(uint32_t baseTtlMs, uint32_t maxTtlMs, std::size_t maxEntries)
    {
        _baseTtlMs = baseTtlMs;
        _maxTtlMs = std::max(baseTtlMs, maxTtlMs);
        _maxEntries = maxEntries ? maxEntries : 1;
    }

    // True when `bot` is told "no route" without a search. Times are wrapping 32-bit milliseconds compared by difference.
    bool Blocked(Key const& key, uint64_t bot, uint32_t nowMs) const
    {
        auto const found = _entries.find(key);
        if (found == _entries.end() || !_baseTtlMs)
            return false;
        Entry const& e = found->second;
        if (uint32_t(nowMs - e.StoredMs) >= TtlOf(e.Failures))
            return false;
        return e.Confirmed || e.FirstBot == bot;
    }

    // A real search failed: the entry is made or its failures grow (so the next entry lives longer).
    void Fail(Key const& key, uint64_t bot, uint32_t nowMs)
    {
        if (!_baseTtlMs)
            return;
        auto found = _entries.find(key);
        if (found == _entries.end())
        {
            if (_entries.size() >= _maxEntries)
                Purge(nowMs);
            _entries.emplace(key, Entry{ nowMs, 1, bot, false });
            return;
        }
        Entry& e = found->second;
        if (bot != e.FirstBot)
            e.Confirmed = true;
        if (e.Failures < 32)
            ++e.Failures;
        e.StoredMs = nowMs;
    }

    // A real search found a route: the memory of this key is gone, so the answer cannot outlive the change.
    void Succeed(Key const& key) { _entries.erase(key); }

    uint32_t TtlOf(uint32_t failures) const
    {
        uint64_t ttl = _baseTtlMs;
        for (uint32_t i = 1; i < failures && ttl < _maxTtlMs; ++i)
            ttl *= 2;
        return uint32_t(std::min<uint64_t>(ttl, _maxTtlMs));
    }

    std::size_t Size() const { return _entries.size(); }
    void Clear() { _entries.clear(); }

private:
    struct Entry
    {
        uint32_t StoredMs;
        uint32_t Failures;
        uint64_t FirstBot;
        bool Confirmed;
    };

    // Full: drop the entries that are long past their time; when that frees nothing the cache is dropped whole.
    void Purge(uint32_t nowMs)
    {
        for (auto it = _entries.begin(); it != _entries.end();)
        {
            if (uint32_t(nowMs - it->second.StoredMs) >= _maxTtlMs)
                it = _entries.erase(it);
            else
                ++it;
        }
        if (_entries.size() >= _maxEntries)
            _entries.clear();
    }

    uint32_t _baseTtlMs = 0;
    uint32_t _maxTtlMs = 0;
    std::size_t _maxEntries = 20000;
    std::unordered_map<Key, Entry, KeyHash> _entries;
};

} // namespace ModuleBotNoPathCache

namespace ModuleBotPathHandoff
{

// Points: the path type of the game (Movement::PointsArray); a template so the test can use a plain vector.
template <class Points>
class Parking
{
public:
    struct Offer
    {
        uint32_t StoredMs = 0;
        float Start[3] = {};
        float Dest[3] = {};
        uint32_t PathType = 0;
        Points Path;
    };

    void Park(uint64_t unit, uint32_t nowMs, float const* start, float const* dest, uint32_t pathType, Points const& path, std::size_t maxEntries)
    {
        if (_parked.size() >= maxEntries && _parked.find(unit) == _parked.end())
            _parked.clear();   // bounded: a bot that logged out leaves one entry until the next Take or this
        Offer& o = _parked[unit];
        o.StoredMs = nowMs;
        std::copy(start, start + 3, o.Start);
        std::copy(dest, dest + 3, o.Dest);
        o.PathType = pathType;
        o.Path = path;
    }

    // Takes the offer out when it is for exactly this unit, start and destination and not older than maxAgeMs (one use, then it is gone either way).
    bool Take(uint64_t unit, uint32_t nowMs, uint32_t maxAgeMs, float const* start, float const* dest, uint32_t& pathType, Points& path)
    {
        auto found = _parked.find(unit);
        if (found == _parked.end())
            return false;
        Offer& o = found->second;
        bool const ok = uint32_t(nowMs - o.StoredMs) <= maxAgeMs && Same(o.Start, start) && Same(o.Dest, dest);
        if (ok)
        {
            pathType = o.PathType;
            path = std::move(o.Path);
        }
        _parked.erase(found);
        return ok;
    }

    void Drop(uint64_t unit) { _parked.erase(unit); }
    std::size_t Size() const { return _parked.size(); }

private:
    static bool Same(float const* a, float const* b) { return a[0] == b[0] && a[1] == b[1] && a[2] == b[2]; }
    std::unordered_map<uint64_t, Offer> _parked;
};

} // namespace ModuleBotPathHandoff

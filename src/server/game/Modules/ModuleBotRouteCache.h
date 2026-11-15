// A short-lived, bounded cache of "does the navmesh have a route from here to that NPC" answers, shared by every bot.
// Pure data structure (no game types) so it is unit tested standalone: see tests/ModuleBotRouteCacheTest.cpp.
//
// Why: the supply/taxi vendor choice and the kMoveTo route check each ran a full Detour search per bot, over and over, for the
// same few vendors from the same few town squares. The answer for (map, vendor spawn, coarse start cell) is the same for every
// bot standing in that cell, so it is asked once. An open route is remembered for openTtlMs, a closed one for closedTtlMs (longer:
// a river or a wall does not move, and a failed search is the most expensive kind, it expands the whole node pool).
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <unordered_map>

namespace ModuleBotRouteCache
{

struct Key
{
    uint32_t MapId = 0;
    uint64_t Target = 0;   // vendor spawn id
    int32_t CellX = 0;
    int32_t CellY = 0;
    bool operator==(Key const& o) const { return MapId == o.MapId && Target == o.Target && CellX == o.CellX && CellY == o.CellY; }
};

struct KeyHash
{
    std::size_t operator()(Key const& k) const
    {
        uint64_t h = k.Target * 0x9E3779B97F4A7C15ull;
        h ^= ((uint64_t(uint32_t(k.CellX)) << 32) | uint32_t(k.CellY)) + 0x7F4A7C15ull + (h << 6) + (h >> 2);
        h ^= uint64_t(k.MapId) * 0xC2B2AE3D27D4EB4Full;
        return std::size_t(h);
    }
};

inline int32_t CellOf(float coord, float cellYards)
{
    return int32_t(std::floor(coord / (cellYards > 1.0f ? cellYards : 1.0f)));
}

inline Key MakeKey(uint32_t mapId, uint64_t target, float x, float y, float cellYards)
{
    return Key{ mapId, target, CellOf(x, cellYards), CellOf(y, cellYards) };
}

// openTtlMs / closedTtlMs of 0 turn the matching kind of answer off (it is not stored). Times are wrapping 32-bit milliseconds
// (GameTime::GetGameTimeMS), compared by difference.
class Cache
{
public:
    // true and `open` set when a live answer is held.
    bool Find(Key const& key, uint32_t nowMs, bool& open)
    {
        auto const found = _entries.find(key);
        if (found == _entries.end())
            return false;
        uint32_t const ttl = found->second.Open ? _openTtlMs : _closedTtlMs;
        if (uint32_t(nowMs - found->second.StoredMs) >= ttl)
        {
            _entries.erase(found);
            return false;
        }
        open = found->second.Open;
        return true;
    }

    void Store(Key const& key, uint32_t nowMs, bool open)
    {
        if ((open ? _openTtlMs : _closedTtlMs) == 0)
            return;
        if (_entries.size() >= _maxEntries && _entries.find(key) == _entries.end())
            Purge(nowMs);
        _entries[key] = Entry{ nowMs, open };
    }

    void Configure(uint32_t openTtlMs, uint32_t closedTtlMs, std::size_t maxEntries)
    {
        _openTtlMs = openTtlMs;
        _closedTtlMs = closedTtlMs;
        _maxEntries = maxEntries ? maxEntries : 1;
    }

    std::size_t Size() const { return _entries.size(); }
    void Clear() { _entries.clear(); }

private:
    struct Entry
    {
        uint32_t StoredMs;
        bool Open;
    };

    // Full: drop the expired entries; when that frees nothing the cache is dropped whole (bounded memory beats a clever eviction).
    void Purge(uint32_t nowMs)
    {
        for (auto it = _entries.begin(); it != _entries.end();)
        {
            uint32_t const ttl = it->second.Open ? _openTtlMs : _closedTtlMs;
            if (uint32_t(nowMs - it->second.StoredMs) >= ttl)
                it = _entries.erase(it);
            else
                ++it;
        }
        if (_entries.size() >= _maxEntries)
            _entries.clear();
    }

    uint32_t _openTtlMs = 600000;
    uint32_t _closedTtlMs = 1800000;
    std::size_t _maxEntries = 50000;
    std::unordered_map<Key, Entry, KeyHash> _entries;
};

// The per-bot memory of the last kMoveTo route answer: modules resend the same destination every tick while a bot walks.
struct MoveAnswer
{
    uint32_t StoredMs = 0;
    bool Valid = false;
    bool Reachable = false;
    int32_t DestX = 0, DestY = 0, DestZ = 0;   // destination in 2 yard cells
    int32_t CellX = 0, CellY = 0;               // start cell
};

inline bool MoveAnswerHolds(MoveAnswer const& a, uint32_t nowMs, uint32_t ttlMs, float dx, float dy, float dz, float sx, float sy, float cellYards)
{
    return a.Valid && ttlMs && uint32_t(nowMs - a.StoredMs) < ttlMs && a.DestX == int32_t(std::floor(dx / 2.0f)) &&
           a.DestY == int32_t(std::floor(dy / 2.0f)) && a.DestZ == int32_t(std::floor(dz / 2.0f)) &&
           a.CellX == CellOf(sx, cellYards) && a.CellY == CellOf(sy, cellYards);
}

inline MoveAnswer MakeMoveAnswer(uint32_t nowMs, bool reachable, float dx, float dy, float dz, float sx, float sy, float cellYards)
{
    return MoveAnswer{ nowMs, true, reachable, int32_t(std::floor(dx / 2.0f)), int32_t(std::floor(dy / 2.0f)), int32_t(std::floor(dz / 2.0f)),
                       CellOf(sx, cellYards), CellOf(sy, cellYards) };
}

} // namespace ModuleBotRouteCache

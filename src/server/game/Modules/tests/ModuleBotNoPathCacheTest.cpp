// Negative route cache (keys, TTL growth, confirmation by a second bot, bound) and the path handoff between the kMoveTo check and MovePoint.
#include "ModuleBotNoPathCache.h"

#include <cstdio>
#include <vector>

namespace
{
int g_failures = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            ++g_failures;                                                 \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                 \
    } while (0)
}

int main()
{
    using namespace ModuleBotNoPathCache;
    Cache c;
    c.Configure(120000, 900000, 4);

    Key const k = MakeKey(36, 100.0f, 200.0f, 10.0f, 50.0f, 60.0f, 8.0f, 8.0f, 40.0f);
    CHECK(!c.Blocked(k, 1, 0));
    c.Fail(k, 1, 1000);
    CHECK(c.Blocked(k, 1, 1000));                 // the bot that failed is told at once
    CHECK(!c.Blocked(k, 2, 1000));                // another bot is not, until a second bot has failed there
    CHECK(c.Blocked(k, 1, 1000 + 119999));
    CHECK(!c.Blocked(k, 1, 1000 + 120000));       // base ttl over: asked again
    c.Fail(k, 2, 130000);                         // second bot, second failure: confirmed, ttl doubles
    CHECK(c.Blocked(k, 3, 130000 + 239999));
    CHECK(!c.Blocked(k, 3, 130000 + 240000));
    CHECK(c.TtlOf(1) == 120000 && c.TtlOf(2) == 240000 && c.TtlOf(3) == 480000 && c.TtlOf(4) == 900000 && c.TtlOf(30) == 900000);
    c.Succeed(k);                                 // a real search found a route: nothing is remembered
    CHECK(!c.Blocked(k, 1, 130001) && c.Size() == 0);

    // Keys: same cell shares, a neighbour cell, another height band, another start cell or another map does not.
    Key const a = MakeKey(36, 100.0f, 200.0f, 10.0f, 50.0f, 60.0f, 8.0f, 8.0f, 40.0f);
    CHECK(a == MakeKey(36, 103.0f, 207.0f, 12.0f, 79.0f, 41.0f, 8.0f, 8.0f, 40.0f));
    CHECK(!(a == MakeKey(36, 108.5f, 200.0f, 10.0f, 50.0f, 60.0f, 8.0f, 8.0f, 40.0f)));
    CHECK(!(a == MakeKey(36, 100.0f, 200.0f, 18.0f, 50.0f, 60.0f, 8.0f, 8.0f, 40.0f)));
    CHECK(!(a == MakeKey(36, 100.0f, 200.0f, 10.0f, 90.0f, 60.0f, 8.0f, 8.0f, 40.0f)));
    CHECK(!(a == MakeKey(1, 100.0f, 200.0f, 10.0f, 50.0f, 60.0f, 8.0f, 8.0f, 40.0f)));
    CHECK(MakeKey(36, 100.0f, 200.0f, 10.0f, 50.0f, 60.0f, 8.0f, 8.0f, 0.0f) == MakeKey(36, 100.0f, 200.0f, 10.0f, 5000.0f, -60.0f, 8.0f, 8.0f, 0.0f));
    CHECK(MakeKey(1, -0.5f, -0.5f, -0.5f, 0, 0, 8.0f, 8.0f, 40.0f).DestX == -1);   // negative coordinates floor

    // Off: nothing stored, nothing served. On again with a bound of 4: the fifth key purges (nothing is old, so the cache is dropped whole).
    Cache off;
    off.Configure(0, 0, 10);
    off.Fail(a, 1, 0);
    CHECK(!off.Blocked(a, 1, 0) && off.Size() == 0);
    Cache bound;
    bound.Configure(1000, 8000, 4);
    for (int i = 0; i < 4; ++i)
        bound.Fail(MakeKey(1, float(i * 100), 0, 0, 0, 0, 8, 8, 40), 1, 10);
    CHECK(bound.Size() == 4);
    bound.Fail(MakeKey(1, 900.0f, 0, 0, 0, 0, 8, 8, 40), 1, 20);
    CHECK(bound.Size() == 1);
    for (int i = 0; i < 3; ++i)
        bound.Fail(MakeKey(1, float(i * 100 + 1000), 0, 0, 0, 0, 8, 8, 40), 1, 20);
    bound.Fail(MakeKey(1, 5000.0f, 0, 0, 0, 0, 8, 8, 40), 1, 20 + 8000);   // long past: the old four are purged, one stays
    CHECK(bound.Size() == 1);

    // Wrapping clock: an entry stored just before 2^32 ms still expires by difference.
    Cache wrap;
    wrap.Configure(1000, 1000, 4);
    wrap.Fail(a, 1, 0xFFFFFF00u);
    CHECK(wrap.Blocked(a, 1, 0xFFFFFF00u + 500));
    CHECK(!wrap.Blocked(a, 1, 0xFFFFFF00u + 1000));

    // Handoff: one use, exact unit/start/destination, bounded age, the path moves with it.
    using Parking = ModuleBotPathHandoff::Parking<std::vector<int>>;
    Parking p;
    float const start[3] = { 1.0f, 2.0f, 3.0f };
    float const dest[3] = { 10.0f, 20.0f, 30.0f };
    float const moved[3] = { 1.5f, 2.0f, 3.0f };
    float const other[3] = { 10.0f, 20.0f, 31.0f };
    uint32_t type = 0;
    std::vector<int> out;
    p.Park(7, 1000, start, dest, 4u, std::vector<int>{ 1, 2, 3 }, 8);
    CHECK(!p.Take(8, 1000, 2000, start, dest, type, out));                 // another unit
    CHECK(p.Take(7, 1500, 2000, start, dest, type, out) && type == 4u && out.size() == 3);
    CHECK(!p.Take(7, 1500, 2000, start, dest, type, out));                 // used once
    p.Park(7, 1000, start, dest, 1u, std::vector<int>{ 1 }, 8);
    CHECK(!p.Take(7, 1000, 2000, moved, dest, type, out) && p.Size() == 0);   // the bot moved: dropped
    p.Park(7, 1000, start, dest, 1u, std::vector<int>{ 1 }, 8);
    CHECK(!p.Take(7, 1000, 2000, start, other, type, out));                // another destination
    p.Park(7, 1000, start, dest, 1u, std::vector<int>{ 1 }, 8);
    CHECK(!p.Take(7, 3001, 2000, start, dest, type, out));                 // too old
    CHECK(p.Size() == 0);
    for (uint64_t u = 0; u < 3; ++u)
        p.Park(u, 0, start, dest, 1u, std::vector<int>{}, 3);
    p.Park(99, 0, start, dest, 1u, std::vector<int>{}, 3);                 // bound reached: dropped whole
    CHECK(p.Size() == 1);

    if (g_failures == 0)
        std::printf("ModuleBotNoPathCacheTest: ok\n");
    return g_failures == 0 ? 0 : 1;
}

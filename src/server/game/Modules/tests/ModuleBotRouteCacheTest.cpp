// Route answer cache: keys, TTLs of open and closed answers, the bound, and the per-bot kMoveTo answer.
#include "ModuleBotRouteCache.h"

#include <cstdio>

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
    using namespace ModuleBotRouteCache;
    Cache cache;
    cache.Configure(1000, 5000, 4);
    bool open = false;

    Key const a = MakeKey(1, 77, 10.0f, 10.0f, 40.0f);
    CHECK(!cache.Find(a, 0, open));
    cache.Store(a, 100, true);
    CHECK(cache.Find(a, 1099, open) && open);
    CHECK(!cache.Find(a, 1100, open));            // open answer expired after its ttl and was dropped
    CHECK(cache.Size() == 0);

    cache.Store(a, 100, false);
    CHECK(cache.Find(a, 5099, open) && !open);    // closed answers live longer
    CHECK(!cache.Find(a, 5100, open));

    // The same cell shares the answer, a neighbour cell, another vendor or another map does not.
    cache.Store(a, 0, true);
    CHECK(cache.Find(MakeKey(1, 77, 35.0f, 5.0f, 40.0f), 10, open));
    CHECK(!cache.Find(MakeKey(1, 77, 45.0f, 5.0f, 40.0f), 10, open));
    CHECK(!cache.Find(MakeKey(1, 78, 10.0f, 10.0f, 40.0f), 10, open));
    CHECK(!cache.Find(MakeKey(2, 77, 10.0f, 10.0f, 40.0f), 10, open));
    // Negative coordinates floor, not truncate: -1 and +1 are different cells.
    CHECK(MakeKey(1, 1, -1.0f, 0.0f, 40.0f).CellX == -1);
    CHECK(MakeKey(1, 1, 1.0f, 0.0f, 40.0f).CellX == 0);

    // Bound: never above 4 entries.
    cache.Clear();
    for (uint64_t t = 0; t < 20; ++t)
    {
        cache.Store(MakeKey(1, t, 0.0f, 0.0f, 40.0f), 10, true);
        CHECK(cache.Size() <= 4);
    }

    // ttl 0 disables a kind.
    cache.Configure(0, 5000, 4);
    cache.Clear();
    cache.Store(a, 0, true);
    CHECK(cache.Size() == 0);
    cache.Store(a, 0, false);
    CHECK(cache.Size() == 1);

    // Wrapping time: stored near the wrap, read after it.
    cache.Configure(1000, 5000, 4);
    cache.Clear();
    cache.Store(a, 0xFFFFFF00u, true);
    CHECK(cache.Find(a, 0x00000100u, open));      // 512 ms later
    CHECK(!cache.Find(a, 0x00000800u, open));

    // The per-bot move answer.
    MoveAnswer const m = MakeMoveAnswer(1000, false, 100.0f, 200.0f, 30.0f, 5.0f, 5.0f, 40.0f);
    CHECK(MoveAnswerHolds(m, 1500, 3000, 100.5f, 200.5f, 30.5f, 7.0f, 7.0f, 40.0f));
    CHECK(!MoveAnswerHolds(m, 4000, 3000, 100.5f, 200.5f, 30.5f, 7.0f, 7.0f, 40.0f));   // expired
    CHECK(!MoveAnswerHolds(m, 1500, 3000, 110.0f, 200.5f, 30.5f, 7.0f, 7.0f, 40.0f));   // other destination
    CHECK(!MoveAnswerHolds(m, 1500, 3000, 100.5f, 200.5f, 30.5f, 50.0f, 7.0f, 40.0f));  // other start cell
    CHECK(!MoveAnswerHolds(m, 1500, 0, 100.5f, 200.5f, 30.5f, 7.0f, 7.0f, 40.0f));      // disabled

    if (g_failures)
        return 1;
    std::printf("ModuleBotRouteCacheTest: ok\n");
    return 0;
}

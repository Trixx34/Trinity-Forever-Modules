#include "ModuleDamageEventRules.h"
#include "tc_catch2.h"

using namespace ModuleDamageEventRules;

TEST_CASE("A killing blow is always sent, a small hit is not", "[ModuleDamageEventRules]")
{
    Config const config;
    CHECK(ShouldSend(config, 1, 5000, true));
    CHECK(ShouldSend(config, 0, 5000, true));          // a kill by 0 effective damage (a duel or script) still is one
    CHECK_FALSE(ShouldSend(config, 100, 5000, false)); // 2%
    CHECK_FALSE(ShouldSend(config, 999, 5000, false)); // just under 20%
}

TEST_CASE("The percent threshold is inclusive and uses the victim's max health", "[ModuleDamageEventRules]")
{
    Config config;
    CHECK(ShouldSend(config, 1000, 5000, false));      // exactly 20%
    CHECK(ShouldSend(config, 4000, 5000, false));
    config.MinPct = 50;
    CHECK_FALSE(ShouldSend(config, 2499, 5000, false));
    CHECK(ShouldSend(config, 2500, 5000, false));
    config.MinPct = 100;
    CHECK_FALSE(ShouldSend(config, 4999, 5000, false));
    CHECK(ShouldSend(config, 5000, 5000, false));
    // no overflow near the 32-bit limit
    CHECK(ShouldSend(config, 0xFFFFFFFFu, 0xFFFFFFFFu, false));
    CHECK_FALSE(ShouldSend(config, 5000, 0, false));   // max health 0 is not a victim
}

TEST_CASE("All sends every hit with damage, Enabled 0 sends nothing", "[ModuleDamageEventRules]")
{
    Config config;
    config.All = true;
    CHECK(ShouldSend(config, 1, 5000, false));
    CHECK_FALSE(ShouldSend(config, 0, 5000, false));   // nothing was dealt
    config.Enabled = false;
    CHECK_FALSE(ShouldSend(config, 5000, 5000, true));
    CHECK_FALSE(ShouldSend(config, 1, 5000, false));
}

TEST_CASE("Overkill and health after", "[ModuleDamageEventRules]")
{
    CHECK(Overkill(300, 100) == 200);
    CHECK(Overkill(100, 100) == 0);
    CHECK(Overkill(50, 100) == 0);
    CHECK(HealthAfter(30, 100) == 70);
    CHECK(HealthAfter(100, 100) == 0);
    CHECK(HealthAfter(300, 100) == 0);
}

TEST_CASE("Sanitize keeps MinPct in 1 to 100", "[ModuleDamageEventRules]")
{
    Config config;
    config.MinPct = 0;
    CHECK(Sanitize(config).MinPct == 1);
    config.MinPct = 500;
    CHECK(Sanitize(config).MinPct == 100);
    config.MinPct = 20;
    CHECK(Sanitize(config).MinPct == 20);
}

TEST_CASE("The limiter lets MaxPerSecond through a window and counts the rest", "[ModuleDamageEventRules]")
{
    Limiter limiter;
    uint32 allowed = 0;
    for (int i = 0; i < 500; ++i)
        if (limiter.Allow(10000 + i, 200)) // 500 events inside 500 ms
            ++allowed;
    CHECK(allowed == 200);
    CHECK(limiter.Dropped == 300);
    CHECK(limiter.TakeUnreported() == 300);
    CHECK(limiter.TakeUnreported() == 0);

    CHECK(limiter.Allow(11000, 200));  // the next window opens a second after the first started
    CHECK(limiter.InWindow == 1);
    CHECK(limiter.Dropped == 300);     // the total survives the window
}

TEST_CASE("The limiter with 0 per second drops everything and survives a clock that went back", "[ModuleDamageEventRules]")
{
    Limiter limiter;
    CHECK_FALSE(limiter.Allow(5000, 0));
    CHECK(limiter.Dropped == 1);

    Limiter wrap;
    CHECK(wrap.Allow(4000000000u, 1));
    CHECK_FALSE(wrap.Allow(4000000001u, 1));
    CHECK(wrap.Allow(5, 1)); // the 32-bit millisecond clock wrapped: a new window
}

#include "tc_catch2.h"
#include "BotMgrRules.h"

#include <string>
#include <vector>

using namespace BotMgrRules;

namespace
{
// A spawn request with nothing wrong, except for what a case changes.
std::string RefusalFor(uint64 botCount, uint32 maxBots, bool stopPassed = false, uint32 worldDiffMs = 0, uint64 dbQueue = 0)
{
    return SpawnRefusal(stopPassed, botCount, maxBots, worldDiffMs, 250, dbQueue, 5000);
}
}

TEST_CASE("A spawn is refused from the bot limit up, not below it", "[BotMgrRules]")
{
    struct Case { uint64 count; uint32 cap; bool refused; };
    Case const cases[] = {
        { 0, 1500, false }, { 1499, 1500, false }, { 1500, 1500, true }, { 1501, 1500, true },
        { 0, 1, false }, { 1, 1, true },
        { 2499, 2500, false }, { 2500, 2500, true }, { 2501, 2500, true },
    };
    for (Case const& c : cases)
    {
        INFO("count " << c.count << " cap " << c.cap);
        CHECK(RefusalFor(c.count, c.cap).empty() == !c.refused);
    }
}

TEST_CASE("The 2026-10-30 sim refusal names the limit of 2500", "[BotMgrRules]")
{
    CHECK(RefusalFor(2500, 2500) == "the bot limit of 2500 is reached");
    CHECK(RefusalFor(2499, 2500).empty());
    // a cap of zero cannot be configured (the config clamps it to 1), but the rule still refuses everything
    CHECK(RefusalFor(0, 0) == "the bot limit of 0 is reached");
}

TEST_CASE("Spawn refusals are checked in a fixed order", "[BotMgrRules]")
{
    // the stop time wins over everything, then the bot limit, then the world interval, then the database queue
    CHECK(RefusalFor(2500, 2500, true, 900, 9000) == "the bot run's stop time has passed");
    CHECK(RefusalFor(2500, 2500, false, 900, 9000) == "the bot limit of 2500 is reached");
    CHECK(RefusalFor(10, 2500, false, 900, 9000) == "the world update interval is 900 ms (limit 250 ms)");
    CHECK(RefusalFor(10, 2500, false, 250, 9000) == "the character database queue has 9000 statements (limit 5000)");
}

TEST_CASE("The world interval and database queue limits are inclusive", "[BotMgrRules]")
{
    CHECK(RefusalFor(0, 10, false, 250).empty());
    CHECK_FALSE(RefusalFor(0, 10, false, 251).empty());
    CHECK(RefusalFor(0, 10, false, 0, 5000).empty());
    CHECK_FALSE(RefusalFor(0, 10, false, 0, 5001).empty());
}

TEST_CASE("A clock time parses as seconds since midnight", "[BotMgrRules]")
{
    CHECK(ParseClockSeconds("00:00") == 0);
    CHECK(ParseClockSeconds("00:01") == 60);
    CHECK(ParseClockSeconds("06:30") == 6 * 3600 + 30 * 60);
    CHECK(ParseClockSeconds("23:59") == 23 * 3600 + 59 * 60);
    CHECK(ParseClockSeconds("23:59:59") == 86399);
    CHECK(ParseClockSeconds("01:02:03") == 3723);
}

TEST_CASE("A malformed stop time parses as minus one", "[BotMgrRules]")
{
    for (char const* text : { "", "6:30", "06:3", "0630", "06-30", "06:30:", "06:30:5", "06:30 ", " 06:30", "24:00", "23:60", "23:59:60",
                              "ab:cd", "06:3x", "-1:30", "06.30.00", "06:30:00:00", "06;30" })
    {
        INFO("text '" << text << "'");
        CHECK(ParseClockSeconds(text) == -1);
    }
}

TEST_CASE("The stop is the next such clock time, over midnight if need be", "[BotMgrRules]")
{
    int32 const h = 3600;
    CHECK(SecondsUntilStop(6 * h, 5 * h) == h);                 // later today
    CHECK(SecondsUntilStop(5 * h, 6 * h) == 23 * h);            // already past: tomorrow
    CHECK(SecondsUntilStop(0, 23 * h + 59 * 60) == 60);         // 00:00 from 23:59 wraps over midnight
    CHECK(SecondsUntilStop(1 * h, 23 * h) == 2 * h);            // 01:00 from 23:00
    CHECK(SecondsUntilStop(23 * h + 59 * 60 + 59, 0) == 86399); // the whole day but a second
    CHECK(SecondsUntilStop(0, 1) == 86399);
}

TEST_CASE("A stop time that is the current second means a full day, never zero", "[BotMgrRules]")
{
    CHECK(SecondsUntilStop(7200, 7200) == 86400);
    CHECK(SecondsUntilStop(0, 0) == 86400);
    for (int32 now : { 0, 1, 3599, 43200, 86399 })
        for (int32 stop : { 0, 1, 3600, 43200, 86399 })
        {
            CHECK(SecondsUntilStop(stop, now) >= 1);
            CHECK(SecondsUntilStop(stop, now) <= 86400);
        }
}

TEST_CASE("The stop time is reached at the stop uptime, only when configured", "[BotMgrRules]")
{
    CHECK_FALSE(StopTimeReached(true, 0, 1000));
    CHECK_FALSE(StopTimeReached(true, 999, 1000));
    CHECK(StopTimeReached(true, 1000, 1000));
    CHECK(StopTimeReached(true, 1001, 1000));
    CHECK_FALSE(StopTimeReached(false, 5000, 1000));
    CHECK_FALSE(StopTimeReached(false, 0, 0));
}

TEST_CASE("A generated name is clipped to twelve characters", "[BotMgrRules]")
{
    std::string name = "Abcdefghijkl";
    ClipName(name);
    CHECK(name == "Abcdefghijkl");
    name = "Abcdefghijklm";
    ClipName(name);
    CHECK(name == "Abcdefghijkl");
    name = "";
    ClipName(name);
    CHECK(name.empty());
    name = "Bal";
    ClipName(name);
    CHECK(name == "Bal");
}

TEST_CASE("The name pool size follows the syllable tables", "[BotMgrRules]")
{
    CHECK(NamePoolSize(18, 8, 13, 0) == uint64(234));
    CHECK(NamePoolSize(18, 8, 13, 1) == uint64(2106));   // with one middle syllable a 2000-bot sim ran out of names
    CHECK(NamePoolSize(18, 8, 13, 2) == uint64(17082));
    CHECK(NamePoolSize(0, 8, 13, 2) == uint64(0));
    CHECK(NamePoolSize(18, 0, 13, 2) == uint64(18) * 13); // no middle syllables to add
}

TEST_CASE("The first valid name is picked, in the order generated", "[BotMgrRules]")
{
    std::vector<std::string> const names = { "Taken", "Reserved", "Free", "AlsoFree" };
    size_t next = 0;
    auto generate = [&] { return names[next++ % names.size()]; };
    auto valid = [](std::string const& name) { return name != "Taken" && name != "Reserved"; };
    CHECK(PickFirstValidName(200, generate, valid) == "Free");
    CHECK(next == 3);
}

TEST_CASE("No name is picked when every attempt is taken", "[BotMgrRules]")
{
    uint32 generated = 0;
    auto generate = [&] { ++generated; return std::string("Taken"); };
    auto valid = [](std::string const&) { return false; };
    CHECK(PickFirstValidName(200, generate, valid).empty());
    CHECK(generated == 200);
}

TEST_CASE("Name picking with zero attempts or a name valid at once", "[BotMgrRules]")
{
    uint32 generated = 0;
    auto generate = [&] { ++generated; return std::string("Bala"); };
    auto always = [](std::string const&) { return true; };
    CHECK(PickFirstValidName(0, generate, always).empty());
    CHECK(generated == 0);
    CHECK(PickFirstValidName(200, generate, always) == "Bala");
    CHECK(generated == 1);
    // the last attempt still counts
    uint32 calls = 0;
    auto lastOnly = [&](std::string const&) { return ++calls == 5; };
    CHECK(PickFirstValidName(5, generate, lastOnly) == "Bala");
}

TEST_CASE("Config defaults are the values in use today", "[BotMgrRules]")
{
    CHECK(LoginMaxPerTick(DEF_LOGIN_MAX_PER_TICK) == 5);
    CHECK(LoginTimeoutMs(DEF_LOGIN_TIMEOUT_MS) == 60000);
    CHECK(ReloginDelayMs(DEF_RELOGIN_DELAY_MS) == 2000);
    CHECK(HunterPetGraceMs(DEF_HUNTER_PET_GRACE_SEC) == 120000);
    CHECK(MailCollectEveryMs(DEF_MAIL_COLLECT_EVERY_SEC) == 30000);
    CHECK(SpreadYards(DEF_SPREAD_SPACING_YARDS) == 3.0f);
    CHECK(SpreadYards(DEF_SPREAD_MAX_RADIUS_YARDS) == 200.0f);
    CHECK(LoginPerSecond(DEF_LOGIN_MAX_PER_SECOND) == 10);
    CHECK(LimitMaxBots(DEF_LIMIT_MAX_BOTS) == 1500);
    CHECK(LimitMaxWorldDiffMs(DEF_LIMIT_MAX_WORLD_DIFF_MS) == 250);
    CHECK(LimitMaxDbQueue(DEF_LIMIT_MAX_DB_QUEUE) == 5000);
    CHECK(BagsItem(DEF_BAGS_ITEM) == 3914);
    CHECK(BagsCount(DEF_BAGS_COUNT, 4) == 4);
    CHECK(HunterStartAmmoStacks(DEF_HUNTER_START_AMMO_STACKS) == 1);
}

TEST_CASE("Config values with a floor are raised to it", "[BotMgrRules]")
{
    CHECK(LoginMaxPerTick(0) == 1);
    CHECK(LoginMaxPerTick(-7) == 1);
    CHECK(LoginMaxPerTick(1) == 1);
    CHECK(LoginMaxPerTick(50) == 50);
    CHECK(LoginTimeoutMs(999) == 1000);
    CHECK(LoginTimeoutMs(-1) == 1000);
    CHECK(LoginTimeoutMs(1000) == 1000);
    CHECK(ReloginDelayMs(-1) == 0);
    CHECK(ReloginDelayMs(0) == 0);
    CHECK(ReloginDelayMs(1) == 1);
    CHECK(LoginPerSecond(0) == 1);
    CHECK(LoginPerSecond(-3) == 1);
    CHECK(LimitMaxBots(0) == 1);
    CHECK(LimitMaxBots(-1) == 1);
    CHECK(LimitMaxBots(2500) == 2500);
    CHECK(LimitMaxWorldDiffMs(9) == 10);
    CHECK(LimitMaxWorldDiffMs(10) == 10);
    CHECK(LimitMaxDbQueue(0) == 1);
    CHECK(LimitMaxDbQueue(1) == 1);
    CHECK(SpreadYards(-1.5f) == 0.0f);
    CHECK(SpreadYards(0.0f) == 0.0f);
    CHECK(SpreadYards(2.5f) == 2.5f);
    CHECK(BagsItem(-5) == 0);
    CHECK(BagsItem(0) == 0);
    CHECK(BagsItem(4496) == 4496);
}

TEST_CASE("Config values with a range are clamped to it", "[BotMgrRules]")
{
    CHECK(HunterPetGraceMs(-1) == 0);
    CHECK(HunterPetGraceMs(0) == 0);
    CHECK(HunterPetGraceMs(3600) == 3600000);
    CHECK(HunterPetGraceMs(3601) == 3600000);
    CHECK(MailCollectEveryMs(0) == 5000);
    CHECK(MailCollectEveryMs(4) == 5000);
    CHECK(MailCollectEveryMs(5) == 5000);
    CHECK(MailCollectEveryMs(3600) == 3600000);
    CHECK(MailCollectEveryMs(100000) == 3600000);
    CHECK(BagsCount(-1, 4) == 0);
    CHECK(BagsCount(0, 4) == 0);
    CHECK(BagsCount(4, 4) == 4);
    CHECK(BagsCount(5, 4) == 4);
    CHECK(HunterStartAmmoStacks(0) == 1);
    CHECK(HunterStartAmmoStacks(-3) == 1);
    CHECK(HunterStartAmmoStacks(10) == 10);
    CHECK(HunterStartAmmoStacks(11) == 10);
}

TEST_CASE("The login token bucket refills at the rate and holds a second's worth", "[BotMgrRules]")
{
    CHECK(RefillLoginTokens(0.0, 0, 10) == 0.0);
    CHECK(RefillLoginTokens(0.0, 100, 10) == Catch::Approx(1.0));
    CHECK(RefillLoginTokens(0.5, 100, 10) == Catch::Approx(1.5));
    CHECK(RefillLoginTokens(9.5, 100, 10) == Catch::Approx(10.0)); // capped at one second's worth
    CHECK(RefillLoginTokens(0.0, 60000, 10) == Catch::Approx(10.0));
    CHECK(RefillLoginTokens(0.0, 1000, 1) == Catch::Approx(1.0));
}

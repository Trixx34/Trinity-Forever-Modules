/*
 * Pure rules of the bot manager (see BotMgr.h): the spawn refusals, the run's stop time, the bot name pool and the clamping of the Bot.*
 * config values. Only numbers and strings, no player, world or database objects, so they can be tested alone.
 */

#ifndef TRINITY_BOT_MGR_RULES_H
#define TRINITY_BOT_MGR_RULES_H

#include "Define.h"
#include <algorithm>
#include <string>

namespace BotMgrRules
{
// ---- Config: the defaults and the clamping of each Bot.* value read by BotMgr::LoadRegistry (and the few read where they are used) ----
inline constexpr int32 DEF_LOGIN_MAX_PER_TICK = 5;
inline constexpr int32 DEF_LOGIN_TIMEOUT_MS = 60000;
inline constexpr int32 DEF_RELOGIN_DELAY_MS = 2000;
inline constexpr int32 DEF_HUNTER_PET_GRACE_SEC = 120;
inline constexpr int32 DEF_MAIL_COLLECT_EVERY_SEC = 30;
inline constexpr float DEF_SPREAD_SPACING_YARDS = 3.0f;
inline constexpr float DEF_SPREAD_MAX_RADIUS_YARDS = 200.0f;
inline constexpr int32 DEF_LOGIN_MAX_PER_SECOND = 10;
inline constexpr int32 DEF_LIMIT_MAX_BOTS = 1500;
inline constexpr int32 DEF_LIMIT_MAX_WORLD_DIFF_MS = 250;
inline constexpr int32 DEF_LIMIT_MAX_DB_QUEUE = 5000;
inline constexpr int32 DEF_BAGS_ITEM = 3914;
inline constexpr int32 DEF_BAGS_COUNT = 4;
inline constexpr int32 DEF_HUNTER_START_AMMO_STACKS = 1;

// Bot.Login.MaxPerTick: at least 1.
inline uint32 LoginMaxPerTick(int32 raw) { return uint32(std::max<int32>(1, raw)); }
// Bot.Login.TimeoutMs: at least 1000.
inline uint32 LoginTimeoutMs(int32 raw) { return uint32(std::max<int32>(1000, raw)); }
// Bot.Relogin.DelayMs: at least 0.
inline uint32 ReloginDelayMs(int32 raw) { return uint32(std::max<int32>(0, raw)); }
// Bot.Hunter.PetGraceSec: 0 to 3600 seconds, returned in milliseconds.
inline uint32 HunterPetGraceMs(int32 rawSec) { return uint32(std::clamp<int32>(rawSec, 0, 3600)) * 1000; }
// Bot.Mail.CollectEverySec: 5 to 3600 seconds, returned in milliseconds.
inline uint32 MailCollectEveryMs(int32 rawSec) { return uint32(std::clamp<int32>(rawSec, 5, 3600)) * 1000; }
// Bot.Spread.SpacingYards and Bot.Spread.MaxRadiusYards: at least 0.
inline float SpreadYards(float raw) { return std::max(0.0f, raw); }
// Bot.Login.MaxPerSecond: at least 1.
inline uint32 LoginPerSecond(int32 raw) { return uint32(std::max<int32>(1, raw)); }
// Bot.Limit.MaxBots: at least 1.
inline uint32 LimitMaxBots(int32 raw) { return uint32(std::max<int32>(1, raw)); }
// Bot.Limit.MaxWorldDiffMs: at least 10.
inline uint32 LimitMaxWorldDiffMs(int32 raw) { return uint32(std::max<int32>(10, raw)); }
// Bot.Limit.MaxDbQueue: at least 1.
inline uint32 LimitMaxDbQueue(int32 raw) { return uint32(std::max<int32>(1, raw)); }
// Bot.Bags.Item: at least 0 (0 turns the bags off).
inline uint32 BagsItem(int64 raw) { return uint32(std::max<int64>(0, raw)); }
// Bot.Bags.Count: 0 up to the number of bag slots.
inline uint32 BagsCount(int64 raw, int64 bagSlots) { return uint32(std::clamp<int64>(raw, 0, bagSlots)); }
// Bot.Hunter.StartAmmo.Stacks: 1 to 10.
inline uint32 HunterStartAmmoStacks(int64 raw) { return uint32(std::clamp<int64>(raw, 1, 10)); }

// ---- Login rate: a token bucket refilled at perSecond tokens a second, at most one second's worth saved up ----
inline double RefillLoginTokens(double tokens, uint32 diffMs, uint32 perSecond)
{
    return std::min<double>(tokens + double(diffMs) * perSecond / 1000.0, double(perSecond));
}

// ---- The run's stop time (Bot.Run.StopTime) ----
// Seconds since local midnight for "HH:MM" or "HH:MM:SS"; -1 if the text is not a clock time.
inline int32 ParseClockSeconds(std::string const& text)
{
    if (text.size() != 5 && text.size() != 8)
        return -1;
    if (text[2] != ':' || (text.size() == 8 && text[5] != ':'))
        return -1;

    auto field = [&text](size_t pos, int32 max) -> int32
    {
        if (text[pos] < '0' || text[pos] > '9' || text[pos + 1] < '0' || text[pos + 1] > '9')
            return -1;
        int32 const value = (text[pos] - '0') * 10 + (text[pos + 1] - '0');
        return value <= max ? value : -1;
    };

    int32 const hours = field(0, 23);
    int32 const minutes = field(3, 59);
    int32 const seconds = text.size() == 8 ? field(6, 59) : 0;
    if (hours < 0 || minutes < 0 || seconds < 0)
        return -1;
    return hours * 3600 + minutes * 60 + seconds;
}

// Seconds from the local clock time nowSec to the next stopSec (both seconds since local midnight): the first such time after now, so a
// stop time that is now, or past, means tomorrow, and 00:00 after 23:59 wraps over midnight. Never 0: at most one day.
inline int32 SecondsUntilStop(int32 stopSec, int32 nowSec)
{
    int32 untilStop = (stopSec - nowSec + 86400) % 86400;
    if (untilStop == 0)
        untilStop = 86400;
    return untilStop;
}

// True once the run has been up as long as the stop time asks for.
inline bool StopTimeReached(bool stopConfigured, uint32 uptimeMs, uint32 stopAtMs)
{
    return stopConfigured && uptimeMs >= stopAtMs;
}

// ---- Spawn refusal ----
// Why a bot is not created now, or an empty string if it may be. The checks are made in this order and the first that fails wins.
// stopPassed: the stop was requested or its time has passed. botCount: the bots registered, whatever their state.
inline std::string SpawnRefusal(bool stopPassed, uint64 botCount, uint32 maxBots, uint32 worldDiffMs, uint32 maxWorldDiffMs,
    uint64 dbQueue, uint32 maxDbQueue)
{
    if (stopPassed)
        return "the bot run's stop time has passed";
    if (botCount >= maxBots)
        return "the bot limit of " + std::to_string(maxBots) + " is reached";
    if (worldDiffMs > maxWorldDiffMs)
        return "the world update interval is " + std::to_string(worldDiffMs) + " ms (limit " + std::to_string(maxWorldDiffMs) + " ms)";
    if (dbQueue > maxDbQueue)
        return "the character database queue has " + std::to_string(dbQueue) + " statements (limit " + std::to_string(maxDbQueue) + ")";
    return std::string();
}

// ---- Bot names ----
// A generated name is cut to this many characters (the client's limit is 12).
inline constexpr size_t MAX_NAME_LENGTH = 12;
// The most names the syllable generator can make: one start, up to maxMiddles middles, one end. With the real tables (18, 8, 13) and
// two middles that is 17082; with one it was 2106 and a sim with 2000 bots used them up. Fewer in practice: clipped names collide.
inline uint64 NamePoolSize(uint32 starts, uint32 middles, uint32 ends, uint32 maxMiddles)
{
    uint64 combinations = 0;
    uint64 perCount = 1;
    for (uint32 count = 0; count <= maxMiddles; ++count)
    {
        combinations += perCount;
        perCount *= middles;
    }
    return uint64(starts) * combinations * ends;
}

inline void ClipName(std::string& name)
{
    if (name.size() > MAX_NAME_LENGTH)
        name.resize(MAX_NAME_LENGTH);
}

// The first generated name that is valid (not taken, not reserved), tried at most maxAttempts times. Empty if none: "no free valid bot name".
template<typename Generate, typename IsValid>
std::string PickFirstValidName(uint32 maxAttempts, Generate generate, IsValid isValid)
{
    for (uint32 attempt = 0; attempt < maxAttempts; ++attempt)
    {
        std::string name = generate();
        if (!isValid(name))
            continue;

        return name;
    }
    return std::string();
}
}

#endif // TRINITY_BOT_MGR_RULES_H

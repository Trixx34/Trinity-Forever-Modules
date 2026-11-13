/*
 * Pure decisions behind the bot damage event (protocol 29, hook PlayerScript.OnDamaged, ModuleMgr::OnBotDamaged): which hits are sent and the per-module rate
 * limit. No Unit, Player or config types, so tests/game/ModuleDamageEventRules.cpp runs without a world.
 */

#ifndef TRINITYCORE_MODULE_DAMAGE_EVENT_RULES_H
#define TRINITYCORE_MODULE_DAMAGE_EVENT_RULES_H

#include "Define.h"

namespace ModuleDamageEventRules
{
// Modules.DamageEvent.* in worldserver.conf.
struct Config
{
    bool Enabled = true;          // Modules.DamageEvent.Enabled
    uint32 MinPct = 20;           // Modules.DamageEvent.MinPct: a hit of at least this percent of the victim's max health is sent (1 to 100)
    bool All = false;             // Modules.DamageEvent.All: every hit is sent (debugging; the rate limit still applies)
    uint32 MaxPerSecond = 200;    // Modules.DamageEvent.MaxPerSecond: per module; 0 = no events
};

// A killing blow is always sent; any other hit when All is set or when amount * 100 >= maxHealth * MinPct. A zero amount that does not kill is never sent.
inline bool ShouldSend(Config const& config, uint32 amount, uint32 maxHealth, bool killingBlow)
{
    if (!config.Enabled)
        return false;
    if (killingBlow)
        return true;
    if (amount == 0)
        return false;
    if (config.All)
        return true;
    if (maxHealth == 0)
        return false;
    return static_cast<uint64>(amount) * 100 >= static_cast<uint64>(maxHealth) * config.MinPct;
}

// The part of the damage beyond the victim's health before the hit.
inline uint32 Overkill(uint32 amount, uint32 healthBefore)
{
    return amount > healthBefore ? amount - healthBefore : 0;
}

inline uint32 HealthAfter(uint32 amount, uint32 healthBefore)
{
    return amount >= healthBefore ? 0 : healthBefore - amount;
}

// Clamps the config values read from worldserver.conf.
inline Config Sanitize(Config config)
{
    if (config.MinPct < 1)
        config.MinPct = 1;
    if (config.MinPct > 100)
        config.MinPct = 100;
    return config;
}

// A fixed one-second window per module: Allow() is true for the first MaxPerSecond calls of a window; the rest are counted in Dropped. Killing blows are not
// exempt (a module that cannot keep up with 200 a second is already losing events), but they are the rarest, so a window rarely fills with anything else.
struct Limiter
{
    uint64 WindowStartMs = 0;
    uint32 InWindow = 0;
    uint64 Dropped = 0;       // total since the module connected
    uint64 DroppedUnreported = 0;

    bool Allow(uint64 nowMs, uint32 maxPerSecond)
    {
        if (nowMs - WindowStartMs >= 1000 || nowMs < WindowStartMs)
        {
            WindowStartMs = nowMs;
            InWindow = 0;
        }
        if (InWindow >= maxPerSecond)
        {
            ++Dropped;
            ++DroppedUnreported;
            return false;
        }
        ++InWindow;
        return true;
    }

    // The drops since the last call, so the caller can log a counter once a window instead of per drop.
    uint64 TakeUnreported()
    {
        uint64 const n = DroppedUnreported;
        DroppedUnreported = 0;
        return n;
    }
};
}

#endif // TRINITYCORE_MODULE_DAMAGE_EVENT_RULES_H

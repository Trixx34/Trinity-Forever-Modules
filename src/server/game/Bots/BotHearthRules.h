/*
 * Pure rules of the bot hearthstone hop (Bot.Hearth.*, ModuleBotActions.cpp TeleportAction): a module teleport that would put a bot into another zone is
 * done the way a player does it instead: the home bind is set to the inn of the destination zone and the bot casts its Hearthstone. Only numbers and flags,
 * no player, map or spell objects, so the tests run without a world.
 */

#ifndef TRINITY_BOT_HEARTH_RULES_H
#define TRINITY_BOT_HEARTH_RULES_H

#include "Define.h"
#include <cmath>
#include <vector>

namespace BotHearthRules
{
inline constexpr uint32 kHearthstoneItem = 6948;
inline constexpr uint32 kHearthstoneSpell = 8690;
inline constexpr uint32 kSkyborneMap = 2991; // the Skyborne leave it only through a quest, never by a hearthstone or a masked teleport

struct Config
{
    bool enabled = false;          // Bot.Hearth.Enabled: the mask is off by default and a teleport is a teleport
    bool fallbackTeleport = false; // Bot.Hearth.FallbackTeleport: stone missing or cooling down: teleport at once; 0 = refuse, the bot walks or waits
    bool grantStone = false;       // Bot.Hearth.GrantStone: a bot without a Hearthstone gets one (an inn hands it out too)
};

struct Situation
{
    bool alive = true;
    bool inCombat = false;
    bool inFlight = false;
    bool onTransport = false;
    bool casting = false;          // already casting the Hearthstone: wait
    bool botMapIsInstance = false; // dungeon, raid, battleground or arena
    bool destMapIsInstance = false;
    uint32 botMap = 0;
    uint32 destMap = 0;
    uint32 botZone = 0;
    uint32 destZone = 0;           // 0 = unknown
    bool hasStone = false;
    bool stoneCooling = false;
    bool innFound = false;         // an innkeeper of the destination zone and the bot's side exists
    bool bindInDestZone = false;   // the bot's home bind already is in the destination zone
};

enum class Outcome
{
    PassThrough, // not a zone change (or a guarded case): the teleport runs as it always did
    Hearth,      // cast the Hearthstone now (bind first unless the bind already is in the zone)
    InProgress,  // the Hearthstone is already being cast: answer applied and do nothing (a module that repeats the teleport is not refused)
    Refuse,      // the hop cannot be done and the fallback is off: the module walks or waits
    Fallback     // the hop cannot be done and the fallback is on: plain teleport
};

struct Decision
{
    Outcome outcome = Outcome::PassThrough;
    bool bind = false;          // set the home bind to the inn first
    bool grant = false;         // give the bot a Hearthstone first
    char const* reason = "";
};

// Guards: never from or to an instance map, a battleground or Skyborne; a dead bot (a ghost's corpse run) and a bot in flight keep the plain path.
inline bool Guarded(Situation const& s)
{
    return !s.alive || s.inFlight || s.onTransport || s.botMapIsInstance || s.destMapIsInstance ||
        s.botMap == kSkyborneMap || s.destMap == kSkyborneMap;
}

inline bool IsZoneChange(Situation const& s)
{
    return s.destZone != 0 && (s.botMap != s.destMap || s.botZone != s.destZone);
}

inline Decision Decide(Config const& c, Situation const& s)
{
    Decision d;
    if (!c.enabled || Guarded(s) || !IsZoneChange(s))
        return d;
    if (s.inCombat)
        return d; // the teleport action refuses a fight with its own reason
    if (s.casting)
    {
        d.outcome = Outcome::InProgress;
        d.reason = "the bot is casting its Hearthstone";
        return d;
    }
    bool const needGrant = !s.hasStone && c.grantStone;
    bool const stoneUsable = (s.hasStone || needGrant) && !s.stoneCooling;
    if (!s.innFound && !s.bindInDestZone)
    {
        d.outcome = c.fallbackTeleport ? Outcome::Fallback : Outcome::Refuse;
        d.reason = "no innkeeper of the destination zone for this side";
        return d;
    }
    if (!stoneUsable)
    {
        d.outcome = c.fallbackTeleport ? Outcome::Fallback : Outcome::Refuse;
        d.reason = s.stoneCooling ? "the Hearthstone is on cooldown" : "the bot has no Hearthstone";
        return d;
    }
    d.outcome = Outcome::Hearth;
    d.bind = !s.bindInDestZone; // a bind that already is in the zone is kept
    d.grant = needGrant;
    return d;
}

struct Inn
{
    uint32 map = 0;
    uint32 zone = 0;
    float x = 0.0f, y = 0.0f, z = 0.0f, o = 0.0f;
    bool sideOk = true; // not hostile to the bot's faction
};

// The nearest usable innkeeper of the zone to a point; -1 when there is none.
inline int PickInn(std::vector<Inn> const& inns, uint32 map, uint32 zone, float x, float y)
{
    int best = -1;
    float bestDist = 0.0f;
    for (size_t i = 0; i < inns.size(); ++i)
    {
        Inn const& in = inns[i];
        if (in.map != map || in.zone != zone || !in.sideOk)
            continue;
        float const dx = in.x - x, dy = in.y - y;
        float const dist = dx * dx + dy * dy;
        if (best < 0 || dist < bestDist)
        {
            best = int(i);
            bestDist = dist;
        }
    }
    return best;
}
}

#endif

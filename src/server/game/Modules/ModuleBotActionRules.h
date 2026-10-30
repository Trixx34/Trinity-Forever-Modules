/*
 * Pure decisions behind the bot action handlers (ModuleBotActions.cpp): which action types exist, the config gate of the scenario-tool actions, the bot-state checks
 * several handlers share, and the cast result to action status table. No Player, Creature or Spell types: only numbers, flags and the layout enums, so the tests
 * run without a world. The reason strings are what the handlers log and send back to the module; they are part of the behaviour, change them only on purpose.
 */

#ifndef TRINITYCORE_MODULE_BOT_ACTION_RULES_H
#define TRINITYCORE_MODULE_BOT_ACTION_RULES_H

#include "Define.h"
#include "ModuleTier2Layout.h"

namespace BotActionRules
{
using forever_module::ActionType;
using forever_module::BotActionStatus;

// An action type from the wire is valid when it is not above the highest one the layout defines (the layout has no gaps from 0 up to it).
inline bool IsValidActionType(uint32 type)
{
    return type <= static_cast<uint32>(ActionType::kLastActionType);
}

// The scenario-tool actions are off unless their config key is on. A refusal is explicit: kRefused and a reason that names the key, so a module (and the log)
// can tell "switched off" from a failed action. 2026-10-27: Bot.Actions.Teleport and Bot.Actions.Grant at 0 refused every forced teleport without a trace.
struct ConfigGateResult
{
    bool allowed;
    BotActionStatus status; // kApplied when allowed (the handler goes on to its own checks), kRefused otherwise
    char const* reason;     // empty when allowed
};

struct ActionConfig
{
    bool teleportAllowed = false;  // Bot.Actions.Teleport
    bool grantAllowed = false;     // Bot.Actions.Grant
    bool useObjectAllowed = false; // Bot.Actions.UseObject
    bool gossipAllowed = false;    // Bot.Actions.Gossip
};

// `enabled` is the value of the type's own config key. Types without a config key are always allowed.
inline ConfigGateResult ConfigGate(ActionType type, bool enabled)
{
    char const* offReason = nullptr;
    switch (type)
    {
        case ActionType::kTeleport: offReason = "Bot.Actions.Teleport is off"; break;
        case ActionType::kGrant: offReason = "Bot.Actions.Grant is off"; break;
        case ActionType::kUseObject: offReason = "Bot.Actions.UseObject is off"; break;
        case ActionType::kGossip: offReason = "Bot.Actions.Gossip is off"; break;
        default: return { true, BotActionStatus::kApplied, "" };
    }
    if (enabled)
        return { true, BotActionStatus::kApplied, "" };
    return { false, BotActionStatus::kRefused, offReason };
}

inline ConfigGateResult ConfigGate(ActionConfig const& config, ActionType type)
{
    switch (type)
    {
        case ActionType::kTeleport: return ConfigGate(type, config.teleportAllowed);
        case ActionType::kGrant: return ConfigGate(type, config.grantAllowed);
        case ActionType::kUseObject: return ConfigGate(type, config.useObjectAllowed);
        case ActionType::kGossip: return ConfigGate(type, config.gossipAllowed);
        default: return ConfigGate(type, true);
    }
}

// The bot-state check of the scenario handlers (UseObject, Gossip): dead first, then in combat, then on a flight. kRefused with the reason, or allowed.
struct StateGateResult
{
    bool allowed;
    BotActionStatus status; // kApplied when allowed, kRefused otherwise
    char const* reason;     // empty when allowed
};

inline StateGateResult AliveIdleGate(bool alive, bool inCombat, bool inFlight)
{
    if (!alive)
        return { false, BotActionStatus::kRefused, "bot is dead" };
    if (inCombat)
        return { false, BotActionStatus::kRefused, "bot is in combat" };
    if (inFlight)
        return { false, BotActionStatus::kRefused, "bot is on a flight" };
    return { true, BotActionStatus::kApplied, "" };
}

// Release the spirit: alive, already a ghost, in combat, in that order.
inline StateGateResult ReleaseSpiritGate(bool alive, bool ghost, bool inCombat)
{
    if (alive)
        return { false, BotActionStatus::kRefused, "bot is alive" };
    if (ghost)
        return { false, BotActionStatus::kRefused, "spirit is already released" };
    if (inCombat)
        return { false, BotActionStatus::kRefused, "bot is in combat" };
    return { true, BotActionStatus::kApplied, "" };
}

// Reclaim the corpse: alive, not a ghost, in that order.
inline StateGateResult ReclaimCorpseGate(bool alive, bool ghost)
{
    if (alive)
        return { false, BotActionStatus::kRefused, "bot is alive" };
    if (!ghost)
        return { false, BotActionStatus::kRefused, "spirit is not released" };
    return { true, BotActionStatus::kApplied, "" };
}

// SpellCastResult values the status table uses (src/server/game/Miscellaneous/SharedDefines.h, enum SpellCastResult). Mirrored here so this header stays free of
// SharedDefines.h; the test compares each one with the enum, so a TrinityCore update that renumbers them fails there.
inline constexpr int32 kCastOk = 0;                  // SPELL_CAST_OK = SPELL_FAILED_SUCCESS
inline constexpr int32 kCastBadImplicitTargets = 12; // SPELL_FAILED_BAD_IMPLICIT_TARGETS
inline constexpr int32 kCastBadTargets = 13;         // SPELL_FAILED_BAD_TARGETS
inline constexpr int32 kCastLineOfSight = 71;        // SPELL_FAILED_LINE_OF_SIGHT
inline constexpr int32 kCastMoving = 75;             // SPELL_FAILED_MOVING
inline constexpr int32 kCastNotKnown = 87;           // SPELL_FAILED_NOT_KNOWN
inline constexpr int32 kCastNotReady = 91;           // SPELL_FAILED_NOT_READY
inline constexpr int32 kCastNoPower = 108;           // SPELL_FAILED_NO_POWER
inline constexpr int32 kCastOutOfRange = 118;        // SPELL_FAILED_OUT_OF_RANGE
inline constexpr int32 kCastRequiresSpellFocus = 123; // SPELL_FAILED_REQUIRES_SPELL_FOCUS
inline constexpr int32 kCastSpellInProgress = 126;   // SPELL_FAILED_SPELL_IN_PROGRESS

// Maps a cast result (the numeric SpellCastResult) to the status a module sees in the next snapshot. Anything not listed is kRefused.
inline BotActionStatus StatusForCastResult(int32 result)
{
    switch (result)
    {
        case kCastOk:
            return BotActionStatus::kApplied;
        case kCastOutOfRange:
            return BotActionStatus::kOutOfRange;
        case kCastNotReady:
            return BotActionStatus::kOnCooldown;
        case kCastNoPower:
            return BotActionStatus::kNotEnoughPower;
        case kCastLineOfSight:
            return BotActionStatus::kNoLineOfSight;
        case kCastBadTargets:
        case kCastBadImplicitTargets:
            return BotActionStatus::kInvalidTarget;
        case kCastNotKnown:
            return BotActionStatus::kNotKnown;
        case kCastMoving:
            return BotActionStatus::kMoving;
        case kCastSpellInProgress:
            return BotActionStatus::kBusy;
        case kCastRequiresSpellFocus:
            return BotActionStatus::kNeedsFocus; // an anvil, a forge or a fire is not near: the module walks to one
        default:
            return BotActionStatus::kRefused;
    }
}
} // namespace BotActionRules

#endif

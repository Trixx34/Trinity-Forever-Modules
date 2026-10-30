#include "ModuleBotActionRules.h"
#include "SharedDefines.h"
#include "tc_catch2.h"

#include <string>

using namespace BotActionRules;
using forever_module::ActionType;
using forever_module::BotActionStatus;

TEST_CASE("Action types are valid up to the last one the layout defines", "[ModuleBotActionRules]")
{
    uint32 const last = static_cast<uint32>(ActionType::kLastActionType);
    CHECK(last == static_cast<uint32>(ActionType::kGossip));
    CHECK(IsValidActionType(0));
    CHECK(IsValidActionType(static_cast<uint32>(ActionType::kCraft)));
    CHECK(IsValidActionType(last - 1));
    CHECK(IsValidActionType(last));
    CHECK_FALSE(IsValidActionType(last + 1));
    CHECK_FALSE(IsValidActionType(0xFFFFFFFFu));

    // every value from 0 to the last one is a real type: the layout has no gaps
    for (uint32 type = 0; type <= last; ++type)
        CHECK(IsValidActionType(type));
}

TEST_CASE("Teleport and Grant at 0 are refused explicitly with a reason naming the key", "[ModuleBotActionRules]")
{
    // 2026-10-27: both keys at 0 silently broke every forced teleport. The refusal must be kRefused with a reason that names the config key.
    ConfigGateResult const teleport = ConfigGate(ActionType::kTeleport, false);
    CHECK_FALSE(teleport.allowed);
    CHECK(teleport.status == BotActionStatus::kRefused);
    CHECK(std::string(teleport.reason) == "Bot.Actions.Teleport is off");

    ConfigGateResult const grant = ConfigGate(ActionType::kGrant, false);
    CHECK_FALSE(grant.allowed);
    CHECK(grant.status == BotActionStatus::kRefused);
    CHECK(std::string(grant.reason) == "Bot.Actions.Grant is off");

    CHECK(std::string(teleport.reason) != std::string(grant.reason));

    // the default config (everything 0) refuses all four, each with its own key in the reason
    ActionConfig const off;
    for (ActionType type : { ActionType::kTeleport, ActionType::kGrant, ActionType::kUseObject, ActionType::kGossip })
    {
        ConfigGateResult const gate = ConfigGate(off, type);
        CHECK_FALSE(gate.allowed);
        CHECK(gate.status == BotActionStatus::kRefused);
        CHECK(std::string(gate.reason).find("Bot.Actions.") == 0);
        CHECK(std::string(gate.reason).find(" is off") != std::string::npos);
    }
}

TEST_CASE("The config gate looks only at the flag of the action's own key", "[ModuleBotActionRules]")
{
    struct Case
    {
        ActionType type;
        ActionConfig config;
        bool allowed;
        char const* reason;
    };

    ActionConfig onlyTeleport; onlyTeleport.teleportAllowed = true;
    ActionConfig onlyGrant; onlyGrant.grantAllowed = true;
    ActionConfig onlyUseObject; onlyUseObject.useObjectAllowed = true;
    ActionConfig onlyGossip; onlyGossip.gossipAllowed = true;

    Case const cases[] = {
        { ActionType::kTeleport, onlyTeleport, true, "" },
        { ActionType::kTeleport, onlyGrant, false, "Bot.Actions.Teleport is off" },
        { ActionType::kGrant, onlyGrant, true, "" },
        { ActionType::kGrant, onlyTeleport, false, "Bot.Actions.Grant is off" },
        { ActionType::kUseObject, onlyUseObject, true, "" },
        { ActionType::kUseObject, onlyGossip, false, "Bot.Actions.UseObject is off" },
        { ActionType::kGossip, onlyGossip, true, "" },
        { ActionType::kGossip, onlyUseObject, false, "Bot.Actions.Gossip is off" },
    };

    for (Case const& c : cases)
    {
        ConfigGateResult const gate = ConfigGate(c.config, c.type);
        CHECK(gate.allowed == c.allowed);
        CHECK(std::string(gate.reason) == c.reason);
        CHECK(gate.status == (c.allowed ? BotActionStatus::kApplied : BotActionStatus::kRefused));
    }
}

TEST_CASE("Action types without a config key are never gated", "[ModuleBotActionRules]")
{
    ActionConfig const off;
    for (uint32 type = 0; type <= static_cast<uint32>(ActionType::kLastActionType); ++type)
    {
        ActionType const t = static_cast<ActionType>(type);
        if (t == ActionType::kTeleport || t == ActionType::kGrant || t == ActionType::kUseObject || t == ActionType::kGossip)
            continue;
        ConfigGateResult const gate = ConfigGate(off, t);
        CHECK(gate.allowed);
        CHECK(std::string(gate.reason).empty());
        CHECK(ConfigGate(t, false).allowed);
    }
}

TEST_CASE("Gossip and UseObject check the bot state in order: dead, combat, flight", "[ModuleBotActionRules]")
{
    struct Case
    {
        bool alive, combat, flight;
        bool allowed;
        char const* reason;
    };

    Case const cases[] = {
        { true, false, false, true, "" },
        { false, false, false, false, "bot is dead" },
        { true, true, false, false, "bot is in combat" },
        { true, false, true, false, "bot is on a flight" },
        // several at once: dead wins over combat, combat over flight
        { false, true, true, false, "bot is dead" },
        { false, true, false, false, "bot is dead" },
        { false, false, true, false, "bot is dead" },
        { true, true, true, false, "bot is in combat" },
    };

    for (Case const& c : cases)
    {
        StateGateResult const gate = AliveIdleGate(c.alive, c.combat, c.flight);
        CHECK(gate.allowed == c.allowed);
        CHECK(std::string(gate.reason) == c.reason);
        CHECK(gate.status == (c.allowed ? BotActionStatus::kApplied : BotActionStatus::kRefused));
    }
}

TEST_CASE("Craft and gather check only dead and combat, dead first", "[ModuleBotActionRules]")
{
    // the craft path passes inFlight = false: a flight does not stop it
    CHECK(AliveIdleGate(true, false, false).allowed);
    CHECK(std::string(AliveIdleGate(false, false, false).reason) == "bot is dead");
    CHECK(std::string(AliveIdleGate(true, true, false).reason) == "bot is in combat");
    CHECK(std::string(AliveIdleGate(false, true, false).reason) == "bot is dead");
}

TEST_CASE("Release spirit refuses alive, ghost, then combat", "[ModuleBotActionRules]")
{
    CHECK(ReleaseSpiritGate(false, false, false).allowed);

    StateGateResult gate = ReleaseSpiritGate(true, false, false);
    CHECK_FALSE(gate.allowed);
    CHECK(gate.status == BotActionStatus::kRefused);
    CHECK(std::string(gate.reason) == "bot is alive");

    gate = ReleaseSpiritGate(false, true, false);
    CHECK_FALSE(gate.allowed);
    CHECK(std::string(gate.reason) == "spirit is already released");

    gate = ReleaseSpiritGate(false, false, true);
    CHECK_FALSE(gate.allowed);
    CHECK(std::string(gate.reason) == "bot is in combat");

    // order: alive beats ghost beats combat
    CHECK(std::string(ReleaseSpiritGate(true, true, true).reason) == "bot is alive");
    CHECK(std::string(ReleaseSpiritGate(false, true, true).reason) == "spirit is already released");
}

TEST_CASE("Reclaim corpse needs a dead ghost", "[ModuleBotActionRules]")
{
    CHECK(ReclaimCorpseGate(false, true).allowed);

    StateGateResult gate = ReclaimCorpseGate(true, true);
    CHECK_FALSE(gate.allowed);
    CHECK(gate.status == BotActionStatus::kRefused);
    CHECK(std::string(gate.reason) == "bot is alive");

    gate = ReclaimCorpseGate(false, false);
    CHECK_FALSE(gate.allowed);
    CHECK(std::string(gate.reason) == "spirit is not released");

    CHECK(std::string(ReclaimCorpseGate(true, false).reason) == "bot is alive");
}

TEST_CASE("The mirrored cast result constants match the TrinityCore enum", "[ModuleBotActionRules]")
{
    CHECK(kCastOk == int32(SPELL_CAST_OK));
    CHECK(kCastBadImplicitTargets == int32(SPELL_FAILED_BAD_IMPLICIT_TARGETS));
    CHECK(kCastBadTargets == int32(SPELL_FAILED_BAD_TARGETS));
    CHECK(kCastLineOfSight == int32(SPELL_FAILED_LINE_OF_SIGHT));
    CHECK(kCastMoving == int32(SPELL_FAILED_MOVING));
    CHECK(kCastNotKnown == int32(SPELL_FAILED_NOT_KNOWN));
    CHECK(kCastNotReady == int32(SPELL_FAILED_NOT_READY));
    CHECK(kCastNoPower == int32(SPELL_FAILED_NO_POWER));
    CHECK(kCastOutOfRange == int32(SPELL_FAILED_OUT_OF_RANGE));
    CHECK(kCastRequiresSpellFocus == int32(SPELL_FAILED_REQUIRES_SPELL_FOCUS));
    CHECK(kCastSpellInProgress == int32(SPELL_FAILED_SPELL_IN_PROGRESS));
}

TEST_CASE("A cast result maps to the status the module sees", "[ModuleBotActionRules]")
{
    struct Case
    {
        SpellCastResult result;
        BotActionStatus status;
    };

    Case const cases[] = {
        { SPELL_CAST_OK, BotActionStatus::kApplied },
        { SPELL_FAILED_OUT_OF_RANGE, BotActionStatus::kOutOfRange },
        { SPELL_FAILED_NOT_READY, BotActionStatus::kOnCooldown },
        { SPELL_FAILED_NO_POWER, BotActionStatus::kNotEnoughPower },
        { SPELL_FAILED_LINE_OF_SIGHT, BotActionStatus::kNoLineOfSight },
        { SPELL_FAILED_BAD_TARGETS, BotActionStatus::kInvalidTarget },
        { SPELL_FAILED_BAD_IMPLICIT_TARGETS, BotActionStatus::kInvalidTarget },
        { SPELL_FAILED_NOT_KNOWN, BotActionStatus::kNotKnown },
        { SPELL_FAILED_MOVING, BotActionStatus::kMoving },
        { SPELL_FAILED_SPELL_IN_PROGRESS, BotActionStatus::kBusy },
        { SPELL_FAILED_REQUIRES_SPELL_FOCUS, BotActionStatus::kNeedsFocus },
        // anything else is a plain refusal
        { SPELL_FAILED_REAGENTS, BotActionStatus::kRefused },
        { SPELL_FAILED_INTERRUPTED, BotActionStatus::kRefused },
        { SPELL_FAILED_UNKNOWN, BotActionStatus::kRefused },
    };

    for (Case const& c : cases)
        CHECK(StatusForCastResult(int32(c.result)) == c.status);

    CHECK(StatusForCastResult(-1) == BotActionStatus::kRefused);
    CHECK(StatusForCastResult(100000) == BotActionStatus::kRefused);
}

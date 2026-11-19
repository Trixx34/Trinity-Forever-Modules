#include "ModuleBotActionRules.h"
#include "SharedDefines.h"
#include "tc_catch2.h"

#include <string>
#include <vector>

using namespace BotActionRules;
using forever_module::ActionType;
using forever_module::BotActionStatus;

TEST_CASE("Action types are valid up to the last one the layout defines", "[ModuleBotActionRules]")
{
    uint32 const last = static_cast<uint32>(ActionType::kLastActionType);
    CHECK(last == static_cast<uint32>(ActionType::kLastActionType));
    CHECK(last >= static_cast<uint32>(ActionType::kLfgJoin)); // the layout only grows
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
        if (t == ActionType::kTeleport || t == ActionType::kGrant || t == ActionType::kUseObject || t == ActionType::kGossip || t == ActionType::kRespecTalents
            || t == ActionType::kStopCast || t == ActionType::kProbePoint || t == ActionType::kTransport)
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

TEST_CASE("TeleportCombatGate: out of combat always passes; in combat only with the key, alone, open world, off a transport", "[ModuleBotActionRules]")
{
    struct Case
    {
        bool combat, key, group, instance, transport;
        bool allowed;
        char const* reason;
    };

    Case const cases[] = {
        // out of combat: the gate never refuses (the other checks of TeleportAction still apply)
        { false, false, false, false, false, true, "" },
        { false, false, true, true, true, true, "" },
        { false, true, true, true, true, true, "" },
        // in combat with the key off: today's refusal, word for word
        { true, false, false, false, false, false, "bot is in combat" },
        { true, false, true, true, true, false, "bot is in combat" },
        // in combat with the key on: a solo bot on an open-world map off a transport may be rescued
        { true, true, false, false, false, true, "" },
        { true, true, true, false, false, false, "bot is in combat and in a group" },
        { true, true, false, true, false, false, "bot is in combat in a dungeon, raid, battleground or arena" },
        { true, true, false, false, true, false, "bot is in combat on a transport" },
        // several at once: group before instance before transport
        { true, true, true, true, true, false, "bot is in combat and in a group" },
        { true, true, false, true, true, false, "bot is in combat in a dungeon, raid, battleground or arena" },
    };

    for (Case const& c : cases)
    {
        StateGateResult const gate = TeleportCombatGate(c.combat, c.key, c.group, c.instance, c.transport);
        CHECK(gate.allowed == c.allowed);
        CHECK(std::string(gate.reason) == c.reason);
        CHECK(gate.status == (c.allowed ? BotActionStatus::kApplied : BotActionStatus::kRefused));
    }
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

TEST_CASE("kTransport: the orders, the board offset, the leave rule and the join range", "[ModuleBotActionRules]")
{
    CHECK_FALSE(IsTransportOrder(0));
    CHECK(IsTransportOrder(kTransportBoard));
    CHECK(IsTransportOrder(kTransportLeave));
    CHECK(IsTransportOrder(kTransportJoinLeader));
    CHECK_FALSE(IsTransportOrder(4));

    // off by default, with the key in the reason
    ConfigGateResult const gate = ConfigGate(ActionType::kTransport, false);
    CHECK_FALSE(gate.allowed);
    CHECK(gate.status == BotActionStatus::kRefused);
    CHECK(std::string(gate.reason) == "Bot.Actions.Transport is off");
    CHECK(ConfigGate(ActionType::kTransport, true).allowed);

    // board: on the deck or the gangplank, not on the next hill, not under the sea
    CHECK(TransportBoardOffsetOk(5.0f, 2.0f, 6.0f, 45.0f));
    CHECK(TransportBoardOffsetOk(-30.0f, 8.0f, 4.0f, 45.0f));
    CHECK_FALSE(TransportBoardOffsetOk(60.0f, 0.0f, 0.0f, 45.0f));   // beyond the range
    CHECK_FALSE(TransportBoardOffsetOk(0.0f, 0.0f, 25.0f, 100.0f));  // too high above the origin
    CHECK_FALSE(TransportBoardOffsetOk(0.0f, 0.0f, -25.0f, 100.0f)); // too far below it
    CHECK_FALSE(TransportBoardOffsetOk(80.0f, 0.0f, 0.0f, 200.0f));  // over the movement handler's 75 yard limit however wide the range
    CHECK_FALSE(TransportBoardOffsetOk(0.0f, -76.0f, 0.0f, 200.0f));
    CHECK(TransportBoardOffsetOk(75.0f, 0.0f, 0.0f, 200.0f));

    // leave: at a dock, or with the ground close below
    CHECK(TransportLeaveAllowed(true, false, 0.0f, 10.0f));
    CHECK(TransportLeaveAllowed(false, true, 3.0f, 10.0f));
    CHECK(TransportLeaveAllowed(false, true, 10.0f, 10.0f));
    CHECK_FALSE(TransportLeaveAllowed(false, true, 40.0f, 10.0f));   // in the middle of the sea
    CHECK_FALSE(TransportLeaveAllowed(false, false, 0.0f, 10.0f));   // no ground known

    CHECK(TransportJoinInRange(60.0f, 100.0f));
    CHECK(TransportJoinInRange(100.0f, 100.0f));
    CHECK_FALSE(TransportJoinInRange(100.5f, 100.0f));
}

TEST_CASE("A refused quest accept names the first check of CanTakeQuest that fails", "[ModuleBotActionRules]")
{
    using forever_module::ActionReason;
    QuestTakeChecks checks;
    CHECK(QuestTakeRefusal(checks) == ActionReason::kNone);

    { QuestTakeChecks c; c.MinLevelOk = false; CHECK(QuestTakeRefusal(c) == ActionReason::kLevelTooLow); }
    { QuestTakeChecks c; c.MaxLevelOk = false; CHECK(QuestTakeRefusal(c) == ActionReason::kLevelTooHigh); }
    { QuestTakeChecks c; c.ClassOk = false; CHECK(QuestTakeRefusal(c) == ActionReason::kWrongClassOrRace); }
    { QuestTakeChecks c; c.RaceOk = false; CHECK(QuestTakeRefusal(c) == ActionReason::kWrongClassOrRace); }
    { QuestTakeChecks c; c.DependentQuestsOk = false; CHECK(QuestTakeRefusal(c) == ActionReason::kPrerequisite); }
    { QuestTakeChecks c; c.ExclusiveGroup = false; CHECK(QuestTakeRefusal(c) == ActionReason::kPrerequisite); }
    { QuestTakeChecks c; c.NotRewarded = false; CHECK(QuestTakeRefusal(c) == ActionReason::kAlreadyDone); }
    { QuestTakeChecks c; c.PeriodOk = false; CHECK(QuestTakeRefusal(c) == ActionReason::kAlreadyDone); }
    { QuestTakeChecks c; c.NotInLog = false; CHECK(QuestTakeRefusal(c) == ActionReason::kAlreadyOnQuest); }
    { QuestTakeChecks c; c.SkillOk = false; CHECK(QuestTakeRefusal(c) == ActionReason::kSkillOrReputation); }
    { QuestTakeChecks c; c.ConditionsOk = false; CHECK(QuestTakeRefusal(c) == ActionReason::kOther); }
    { QuestTakeChecks c; c.NotDisabled = false; CHECK(QuestTakeRefusal(c) == ActionReason::kOther); }

    // several failures: the order of CanTakeQuest decides (status before class before level before prerequisites)
    QuestTakeChecks many;
    many.MinLevelOk = false;
    many.DependentQuestsOk = false;
    many.ClassOk = false;
    CHECK(QuestTakeRefusal(many) == ActionReason::kWrongClassOrRace);
    many.ClassOk = true;
    CHECK(QuestTakeRefusal(many) == ActionReason::kLevelTooLow);
    many.NotRewarded = false;
    CHECK(QuestTakeRefusal(many) == ActionReason::kAlreadyDone);

    CHECK(QuestAddRefusal(false) == ActionReason::kLogFull);
    CHECK(QuestAddRefusal(true) == ActionReason::kBagsFull);
}

TEST_CASE("The action result's reason fits the reserved 16 bits and starts at none", "[ModuleBotActionRules]")
{
    forever_module::ActionResult result;
    CHECK(result.reason == 0);
    CHECK(sizeof(result) == 12);
    CHECK(static_cast<uint32>(forever_module::ActionReason::kNone) == 0);
}

TEST_CASE("SpiritHealerGate: alive, not a ghost, config, hardcore, then the world decides", "[ModuleBotActionRules]")
{
    // SpiritHealerGate(alive, ghost, enabled, hardcore, healerFound, inInteractionRange)
    SpiritHealerResult result = SpiritHealerGate(true, false, true, false, true, true);
    CHECK(result.step == SpiritHealerStep::kRefuse);
    CHECK(result.status == BotActionStatus::kRefused);
    CHECK(std::string(result.reason) == "bot is alive");

    result = SpiritHealerGate(true, true, true, false, true, true); // alive wins over everything
    CHECK(std::string(result.reason) == "bot is alive");

    result = SpiritHealerGate(false, false, true, false, true, true);
    CHECK(result.step == SpiritHealerStep::kRefuse);
    CHECK(result.status == BotActionStatus::kRefused);
    CHECK(std::string(result.reason) == "spirit is not released");

    result = SpiritHealerGate(false, true, false, false, true, true);
    CHECK(result.step == SpiritHealerStep::kRefuse);
    CHECK(result.status == BotActionStatus::kRefused);
    CHECK(std::string(result.reason) == "Bot.Actions.SpiritHealer is off");

    result = SpiritHealerGate(false, true, true, true, true, true);
    CHECK(result.step == SpiritHealerStep::kRefuse);
    CHECK(std::string(result.reason) == "hardcore realm: death is permanent");

    // the config key is checked before the hardcore realm
    CHECK(std::string(SpiritHealerGate(false, true, false, true, true, true).reason) == "Bot.Actions.SpiritHealer is off");
}

TEST_CASE("SpiritHealerGate: no healer falls back to the reclaim, far walks, near takes", "[ModuleBotActionRules]")
{
    SpiritHealerResult result = SpiritHealerGate(false, true, true, false, false, false);
    CHECK(result.step == SpiritHealerStep::kFallbackReclaim);
    CHECK(result.status == BotActionStatus::kApplied);
    CHECK(std::string(result.reason).empty());

    result = SpiritHealerGate(false, true, true, false, true, false);
    CHECK(result.step == SpiritHealerStep::kWalk);
    CHECK(result.status == BotActionStatus::kMoving);
    CHECK(std::string(result.reason).empty());

    result = SpiritHealerGate(false, true, true, false, true, true);
    CHECK(result.step == SpiritHealerStep::kTake);
    CHECK(result.status == BotActionStatus::kApplied);
    CHECK(std::string(result.reason).empty());
}

TEST_CASE("kResurrect has no config-gate key: the spirit healer key is checked inside SpiritHealerGate", "[ModuleBotActionRules]")
{
    CHECK(ConfigGate(ActionType::kResurrect, false).allowed);
}

TEST_CASE("ResolveCastRank: dest_x >= 1 casts exactly the rank asked for, else the highest known", "[ModuleBotActionRules]")
{
    // ResolveCastRank(destX, spellId, exactKnown, highestKnown)
    CHECK(ResolveCastRank(0.0f, 2050, false, 2054) == 2054u);   // no request: the highest known rank
    CHECK(ResolveCastRank(0.0f, 2050, true, 2054) == 2054u);
    CHECK(ResolveCastRank(0.0f, 2050, false, 0u) == 0u);        // knows none: kNotKnown
    CHECK(ResolveCastRank(0.5f, 2050, true, 2054) == 2054u);    // under 1 is no request
    CHECK(ResolveCastRank(-3.0f, 2050, true, 2054) == 2054u);
    CHECK(ResolveCastRank(1.0f, 2050, true, 2054) == 2050u);    // exact and known: that rank
    CHECK(ResolveCastRank(7.0f, 2050, true, 0u) == 2050u);
    CHECK(ResolveCastRank(1.0f, 2050, false, 2054) == 0u);      // exact and not known: 0, never the highest rank
}

TEST_CASE("PlanRepair: a bot that cannot pay the whole bill repairs what it can, worn and worst first", "[ModuleBotActionRules]")
{
    // 2026-11-14: kVendor called DurabilityRepairAll, which repaired nothing when the bill was more than the bot's money.
    std::vector<RepairPiece> const pieces = {
        { 40, 100, 30, true },   // 0: worn, 40 %
        { 0, 50, 50, true },     // 1: worn, broken
        { 0, 80, 5, false },     // 2: in the bags, broken
        { 90, 100, 10, true },   // 3: worn, 90 %
        { 100, 100, 0, true },   // 4: not damaged (no cost): never listed
    };

    SECTION("enough money: everything damaged, worn first, worst first")
    {
        RepairPlan const plan = PlanRepair(pieces, 1000);
        CHECK(plan.Repair == std::vector<size_t>{ 1, 0, 3, 2 });
        CHECK(plan.Spent == 95u);
        CHECK(plan.Skipped == 0u);
    }
    SECTION("short of money: the broken worn piece first, an unaffordable one skipped, a cheaper one after it still repaired")
    {
        RepairPlan const plan = PlanRepair(pieces, 65);
        CHECK(plan.Repair == std::vector<size_t>{ 1, 3, 2 }); // 50, then 30 is too much (15 left), 10, then 5
        CHECK(plan.Spent == 65u);
        CHECK(plan.Skipped == 1u);
    }
    SECTION("no money: nothing, every damaged piece skipped")
    {
        RepairPlan const plan = PlanRepair(pieces, 0);
        CHECK(plan.Repair.empty());
        CHECK(plan.Spent == 0u);
        CHECK(plan.Skipped == 4u);
    }
}

TEST_CASE("RepairsBefore: durability compared as a fraction, a tie goes to the cheaper piece", "[ModuleBotActionRules]")
{
    CHECK(RepairsBefore({ 10, 100, 50, true }, { 30, 60, 1, true }));      // 10 % before 50 %, whatever the price
    CHECK_FALSE(RepairsBefore({ 30, 60, 1, true }, { 10, 100, 50, true }));
    CHECK(RepairsBefore({ 25, 50, 5, true }, { 50, 100, 9, true }));       // both 50 %: the cheaper one
    CHECK_FALSE(RepairsBefore({ 50, 100, 9, true }, { 25, 50, 5, true }));
    CHECK(RepairsBefore({ 99, 100, 9, true }, { 0, 100, 1, false }));      // worn before the bags, even a broken bag item
    CHECK_FALSE(RepairsBefore({ 5, 10, 3, true }, { 5, 10, 3, true }));    // equal: not before (strict weak order)
}

namespace
{
// A mount item as the handler describes it: usable, one ON_LEARN effect for a ground mount the bot does not know.
MountItemCandidate MountItemFor(uint32 spell, bool usable = true, bool known = false)
{
    MountItemCandidate candidate;
    candidate.MountItem = true;
    candidate.Usable = usable;
    candidate.Effects.push_back({ true, spell, known, true });
    return candidate;
}
}

TEST_CASE("PickMountItem: nothing to learn from an empty bag or from items that are not mounts", "[ModuleBotActionRules]")
{
    CHECK(PickMountItem({}).Index == -1);
    CHECK(PickMountItem({}).Spells.empty());

    MountItemCandidate notMount = MountItemFor(6777);
    notMount.MountItem = false;
    CHECK(PickMountItem({ notMount }).Index == -1);
}

TEST_CASE("PickMountItem: an item the bot may not use, or whose spell it knows, is skipped", "[ModuleBotActionRules]")
{
    CHECK(PickMountItem({ MountItemFor(6777, false) }).Index == -1);   // level, faction, class or riding refused it
    CHECK(PickMountItem({ MountItemFor(6777, true, true) }).Index == -1); // already known: no second learn, the item stays
}

TEST_CASE("PickMountItem: the first usable mount wins and only its learnable spells are returned", "[ModuleBotActionRules]")
{
    MountItemPick const pick = PickMountItem({ MountItemFor(1111, false), MountItemFor(6777), MountItemFor(8394) });
    CHECK(pick.Index == 1);
    CHECK(pick.Spells == std::vector<uint32>{ 6777 });
}

TEST_CASE("PickMountItem: search and learn share one predicate (ON_LEARN, ground mount, not known)", "[ModuleBotActionRules]")
{
    MountItemCandidate candidate;
    candidate.MountItem = true;
    candidate.Usable = true;
    candidate.Effects.push_back({ false, 55884, false, true }); // a USE effect (Learning): ignored
    candidate.Effects.push_back({ true, 4001, false, false });  // ON_LEARN but not a ground mount (flying or no mounted aura): ignored
    candidate.Effects.push_back({ true, 4002, true, true });    // known already: ignored
    candidate.Effects.push_back({ true, 4003, false, true });   // the one
    candidate.Effects.push_back({ true, 4004, false, true });   // a second mount spell of the same item
    MountItemPick const pick = PickMountItem({ candidate });
    CHECK(pick.Index == 0);
    CHECK(pick.Spells == std::vector<uint32>{ 4003, 4004 });

    CHECK(IsLearnableMountEffect({ true, 1, false, true }));
    CHECK_FALSE(IsLearnableMountEffect({ false, 1, false, true }));
    CHECK_FALSE(IsLearnableMountEffect({ true, 1, true, true }));
    CHECK_FALSE(IsLearnableMountEffect({ true, 1, false, false }));
}

TEST_CASE("PickMountItem: an item with only a USE effect is not learned from", "[ModuleBotActionRules]")
{
    MountItemCandidate candidate;
    candidate.MountItem = true;
    candidate.Usable = true;
    candidate.Effects.push_back({ false, 6777, false, true });
    CHECK(PickMountItem({ candidate }).Index == -1);
}

TEST_CASE("MountLearnSucceeded: the item is used up only when a spell is known afterwards", "[ModuleBotActionRules]")
{
    CHECK_FALSE(MountLearnSucceeded({}));
    CHECK_FALSE(MountLearnSucceeded({ false }));
    CHECK_FALSE(MountLearnSucceeded({ false, false }));
    CHECK(MountLearnSucceeded({ true }));
    CHECK(MountLearnSucceeded({ false, true }));
}

TEST_CASE("Mount learn reasons are fixed text the module log shows", "[ModuleBotActionRules]")
{
    CHECK(std::string(kNoMountItemReason) == "bot has no mount and no usable mount item in its bags");
    CHECK(std::string(kMountLearnFailedReason) == "the mount item taught no spell, kept in the bags");
}

TEST_CASE("A spawn reference names a service NPC of the same entry only", "[ModuleBotActionRules]")
{
    using BotActionRules::SpawnRefMatches;
    CHECK(SpawnRefMatches(8719, 8719, 0x1000));        // an auctioneer of the entry the snapshot named
    CHECK_FALSE(SpawnRefMatches(8719, 8720, 0x1000));  // another entry on that spawn id
    CHECK_FALSE(SpawnRefMatches(1234, 1234, 0));       // a mob: a reference to one that is gone must not hit another spawn
}

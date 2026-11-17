#include "tc_catch2.h"
#include "BotHearthRules.h"

using namespace BotHearthRules;

namespace
{
Config On() { Config c; c.enabled = true; return c; }

// A bot in Elwynn (zone 12) sent to Westfall (zone 40), Eastern Kingdoms, with a usable stone and an inn there.
Situation Hop()
{
    Situation s;
    s.botMap = 0; s.destMap = 0; s.botZone = 12; s.destZone = 40;
    s.hasStone = true; s.innFound = true;
    return s;
}
}

TEST_CASE("The hearth mask is off by default", "[BotHearthRules]")
{
    CHECK(Decide(Config{}, Hop()).outcome == Outcome::PassThrough);
}

TEST_CASE("A zone change binds to the destination inn and hearths", "[BotHearthRules]")
{
    Decision const d = Decide(On(), Hop());
    CHECK(d.outcome == Outcome::Hearth);
    CHECK(d.bind);
    CHECK_FALSE(d.grant);
}

TEST_CASE("A bind already in the destination zone is kept", "[BotHearthRules]")
{
    Situation s = Hop();
    s.bindInDestZone = true;
    s.innFound = false;
    Decision const d = Decide(On(), s);
    CHECK(d.outcome == Outcome::Hearth);
    CHECK_FALSE(d.bind);
}

TEST_CASE("The same zone, an unknown zone and the same map and zone pass through", "[BotHearthRules]")
{
    Situation s = Hop();
    s.destZone = s.botZone;
    CHECK(Decide(On(), s).outcome == Outcome::PassThrough);
    s = Hop();
    s.destZone = 0;
    CHECK(Decide(On(), s).outcome == Outcome::PassThrough);
    s = Hop();
    s.destMap = 1; s.destZone = 12; // the same zone id on another map is a zone change
    CHECK(Decide(On(), s).outcome == Outcome::Hearth);
}

TEST_CASE("Instances, battlegrounds, Skyborne, ghosts and flights keep the plain teleport", "[BotHearthRules]")
{
    Situation s = Hop(); s.botMapIsInstance = true;
    CHECK(Decide(On(), s).outcome == Outcome::PassThrough);
    s = Hop(); s.destMapIsInstance = true;
    CHECK(Decide(On(), s).outcome == Outcome::PassThrough);
    s = Hop(); s.botMap = kSkyborneMap;
    CHECK(Decide(On(), s).outcome == Outcome::PassThrough);
    s = Hop(); s.destMap = kSkyborneMap;
    CHECK(Decide(On(), s).outcome == Outcome::PassThrough);
    s = Hop(); s.alive = false;
    CHECK(Decide(On(), s).outcome == Outcome::PassThrough);
    s = Hop(); s.inFlight = true;
    CHECK(Decide(On(), s).outcome == Outcome::PassThrough);
    s = Hop(); s.onTransport = true;
    CHECK(Decide(On(), s).outcome == Outcome::PassThrough);
}

TEST_CASE("A missing or cooling stone refuses, or teleports when the fallback is on", "[BotHearthRules]")
{
    Situation s = Hop(); s.hasStone = false;
    CHECK(Decide(On(), s).outcome == Outcome::Refuse);
    s.stoneCooling = false;
    Config c = On(); c.fallbackTeleport = true;
    CHECK(Decide(c, s).outcome == Outcome::Fallback);
    s = Hop(); s.stoneCooling = true;
    CHECK(Decide(On(), s).outcome == Outcome::Refuse);
    CHECK(Decide(c, s).outcome == Outcome::Fallback);
}

TEST_CASE("A bot without a stone is given one when the key allows it", "[BotHearthRules]")
{
    Situation s = Hop(); s.hasStone = false;
    Config c = On(); c.grantStone = true;
    Decision const d = Decide(c, s);
    CHECK(d.outcome == Outcome::Hearth);
    CHECK(d.grant);
    s.stoneCooling = true; // a new stone does not cure a cooldown of the spell
    CHECK(Decide(c, s).outcome == Outcome::Refuse);
}

TEST_CASE("No inn in the destination zone is a refusal or a fallback", "[BotHearthRules]")
{
    Situation s = Hop(); s.innFound = false;
    CHECK(Decide(On(), s).outcome == Outcome::Refuse);
    Config c = On(); c.fallbackTeleport = true;
    CHECK(Decide(c, s).outcome == Outcome::Fallback);
}

TEST_CASE("A bot already casting the stone is not started again, a bot in combat passes through to the combat refusal", "[BotHearthRules]")
{
    Situation s = Hop(); s.casting = true;
    CHECK(Decide(On(), s).outcome == Outcome::InProgress);
    s = Hop(); s.inCombat = true;
    CHECK(Decide(On(), s).outcome == Outcome::PassThrough);
}

TEST_CASE("The nearest inn of the zone and side is picked", "[BotHearthRules]")
{
    std::vector<Inn> inns = {
        { 0, 40, 100.f, 0.f, 0.f, 0.f, true },
        { 0, 40, 10.f, 0.f, 0.f, 0.f, false },  // hostile side
        { 0, 12, 1.f, 0.f, 0.f, 0.f, true },    // other zone
        { 1, 40, 2.f, 0.f, 0.f, 0.f, true },    // other map
        { 0, 40, 50.f, 0.f, 0.f, 0.f, true },
    };
    CHECK(PickInn(inns, 0, 40, 0.f, 0.f) == 4);
    CHECK(PickInn(inns, 0, 99, 0.f, 0.f) == -1);
    CHECK(PickInn({}, 0, 40, 0.f, 0.f) == -1);
}

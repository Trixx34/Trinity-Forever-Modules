#include "ModuleBotSnapshotRules.h"
#include "tc_catch2.h"

#include <cstddef>
#include <functional>
#include <initializer_list>
#include <limits>
#include <vector>

namespace Rules = ModuleBotSnapshotRules;

TEST_CASE("Snapshot clamp to uint32 saturates unsigned values", "[ModuleBotSnapshotRules]")
{
    constexpr uint64 max32 = std::numeric_limits<uint32>::max();
    CHECK(Rules::ClampToUint32(uint64(0)) == 0u);
    CHECK(Rules::ClampToUint32(uint64(1)) == 1u);
    CHECK(Rules::ClampToUint32(max32 - 1) == uint32(max32 - 1));
    CHECK(Rules::ClampToUint32(max32) == uint32(max32));
    CHECK(Rules::ClampToUint32(max32 + 1) == uint32(max32));
    CHECK(Rules::ClampToUint32(std::numeric_limits<uint64>::max()) == uint32(max32));
}

TEST_CASE("Snapshot clamp to uint32 maps negative signed values to zero", "[ModuleBotSnapshotRules]")
{
    constexpr int64 max32 = std::numeric_limits<uint32>::max();
    CHECK(Rules::ClampToUint32(int64(std::numeric_limits<int64>::min())) == 0u);
    CHECK(Rules::ClampToUint32(int64(-1)) == 0u);
    CHECK(Rules::ClampToUint32(int64(0)) == 0u);
    CHECK(Rules::ClampToUint32(int64(1)) == 1u);
    CHECK(Rules::ClampToUint32(max32) == uint32(max32));
    CHECK(Rules::ClampToUint32(max32 + 1) == uint32(max32));
    CHECK(Rules::ClampToUint32(std::numeric_limits<int64>::max()) == uint32(max32));
}

TEST_CASE("Snapshot cooldown remaining saturates at 32 bits", "[ModuleBotSnapshotRules]")
{
    constexpr int64 max32 = std::numeric_limits<uint32>::max();
    CHECK(Rules::ClampCooldownRemaining(1) == 1u);
    CHECK(Rules::ClampCooldownRemaining(max32) == uint32(max32));
    CHECK(Rules::ClampCooldownRemaining(max32 + 1) == uint32(max32));
    CHECK(Rules::ClampCooldownRemaining(std::numeric_limits<int64>::max()) == uint32(max32));
}

TEST_CASE("Snapshot cooldowns sort closest to expiring first and are capped to the slots", "[ModuleBotSnapshotRules]")
{
    std::vector<Rules::ActiveCooldown> active = { { 10, 5000 }, { 11, 100 }, { 12, 3000 }, { 13, 1 }, { 14, 90000 } };
    Rules::SortCooldowns(active);
    REQUIRE(active.size() == 5u);
    CHECK(active[0].SpellId == 13);
    CHECK(active[1].SpellId == 11);
    CHECK(active[2].SpellId == 12);
    CHECK(active[3].SpellId == 10);
    CHECK(active[4].SpellId == 14);

    CHECK(Rules::CapCount(0, 4) == 0u);
    CHECK(Rules::CapCount(3, 4) == 3u);
    CHECK(Rules::CapCount(4, 4) == 4u);
    CHECK(Rules::CapCount(5, 4) == 4u);
    CHECK(Rules::CapCount(100, 0) == 0u);

    std::vector<Rules::ActiveCooldown> none;
    Rules::SortCooldowns(none);
    CHECK(none.empty());
}

namespace
{
constexpr std::size_t kRing = 8;

// Records hits the way RecordBotDamage does: write at Next, bump Sequence, advance Next.
struct TestRing
{
    uint32 Hits[kRing] = {};
    std::size_t Next = 0;
    uint32 Sequence = 0;
    uint32 LifeStart = 0;

    void Push()
    {
        Hits[Next] = ++Sequence; // the stored value is the hit's sequence
        Next = Rules::NextDamageSlot(Next, kRing);
    }

    // Reports the hits the way BuildSnapshotBot does, oldest first.
    std::vector<uint32> Report() const
    {
        uint8 const count = Rules::DamageHitCount(Sequence, LifeStart, kRing);
        std::vector<uint32> out;
        for (uint8 i = 0; i < count; ++i)
            out.push_back(Hits[Rules::DamageRingSlot(Next, kRing, count, i)]);
        return out;
    }
};
}

TEST_CASE("Damage ring reports nothing before the first hit", "[ModuleBotSnapshotRules]")
{
    TestRing ring;
    CHECK(Rules::DamageHitCount(0, 0, kRing) == 0);
    CHECK(ring.Report().empty());
}

TEST_CASE("Damage ring with fewer than eight hits lists them oldest first", "[ModuleBotSnapshotRules]")
{
    for (uint32 hits = 1; hits < kRing; ++hits)
    {
        TestRing ring;
        for (uint32 i = 0; i < hits; ++i)
            ring.Push();
        std::vector<uint32> const report = ring.Report();
        REQUIRE(report.size() == hits);
        for (uint32 i = 0; i < hits; ++i)
            CHECK(report[i] == i + 1);
    }
}

TEST_CASE("Damage ring with exactly eight hits is full and still in order", "[ModuleBotSnapshotRules]")
{
    TestRing ring;
    for (uint32 i = 0; i < kRing; ++i)
        ring.Push();
    CHECK(ring.Next == 0u); // wrapped back to the first slot
    std::vector<uint32> const report = ring.Report();
    REQUIRE(report.size() == kRing);
    for (uint32 i = 0; i < kRing; ++i)
        CHECK(report[i] == i + 1);
}

TEST_CASE("Damage ring overwrites the oldest hit when it wraps and keeps the sequence monotonic", "[ModuleBotSnapshotRules]")
{
    for (uint32 total : { 9u, 10u, 15u, 16u, 17u, 100u })
    {
        TestRing ring;
        uint32 last = 0;
        for (uint32 i = 0; i < total; ++i)
        {
            ring.Push();
            CHECK(ring.Sequence == last + 1);
            last = ring.Sequence;
        }
        std::vector<uint32> const report = ring.Report();
        REQUIRE(report.size() == kRing);
        for (uint32 i = 0; i < kRing; ++i)
            CHECK(report[i] == total - kRing + 1 + i); // the newest eight, oldest first
    }
}

TEST_CASE("Damage ring reports only the hits since the current life started", "[ModuleBotSnapshotRules]")
{
    TestRing ring;
    for (uint32 i = 0; i < 12; ++i)
        ring.Push();
    ring.LifeStart = ring.Sequence; // the bot died and is alive again
    CHECK(ring.Report().empty());

    ring.Push();
    ring.Push();
    ring.Push();
    std::vector<uint32> const report = ring.Report();
    REQUIRE(report.size() == 3u);
    CHECK(report[0] == 13);
    CHECK(report[1] == 14);
    CHECK(report[2] == 15);

    CHECK(Rules::DamageHitCount(12, 4, kRing) == 8);
    CHECK(Rules::DamageHitCount(13, 4, kRing) == 8); // never more than the ring holds
    CHECK(Rules::DamageHitCount(11, 4, kRing) == 7);
}

TEST_CASE("Damage ring slot arithmetic stays inside the ring", "[ModuleBotSnapshotRules]")
{
    CHECK(Rules::NextDamageSlot(0, kRing) == 1u);
    CHECK(Rules::NextDamageSlot(7, kRing) == 0u);
    CHECK(Rules::DamageRingSlot(0, kRing, 8, 0) == 0u);
    CHECK(Rules::DamageRingSlot(3, kRing, 8, 0) == 3u);
    CHECK(Rules::DamageRingSlot(3, kRing, 2, 0) == 1u);
    CHECK(Rules::DamageRingSlot(3, kRing, 2, 1) == 2u);
    CHECK(Rules::DamageRingSlot(0, kRing, 2, 0) == 6u);
    CHECK(Rules::DamageRingSlot(0, kRing, 2, 1) == 7u);
}

namespace
{
struct TestUnit
{
    int Id;
    float Dist;
};

bool CloserFirst(TestUnit const& a, TestUnit const& b)
{
    return a.Dist < b.Dist;
}

std::vector<TestUnit> Make(int firstId, std::initializer_list<float> dists)
{
    std::vector<TestUnit> out;
    int id = firstId;
    for (float d : dists)
        out.push_back({ id++, d });
    return out;
}
}

TEST_CASE("Nearby prefix sort puts the nearest units first up to the limit", "[ModuleBotSnapshotRules]")
{
    for (std::size_t limit : { std::size_t(0), std::size_t(1), std::size_t(3), std::size_t(5), std::size_t(6), std::size_t(50) })
    {
        std::vector<TestUnit> units = Make(1, { 30.f, 5.f, 20.f, 1.f, 10.f });
        Rules::SortNearestPrefix(units, limit, CloserFirst);
        REQUIRE(units.size() == 5u);
        std::size_t const prefix = limit < 5 ? limit : 5;
        float const expected[5] = { 1.f, 5.f, 10.f, 20.f, 30.f };
        for (std::size_t i = 0; i < prefix; ++i)
            CHECK(units[i].Dist == expected[i]);
    }

    std::vector<TestUnit> empty;
    Rules::SortNearestPrefix(empty, 4, CloserFirst);
    CHECK(empty.empty());
}

TEST_CASE("Nearby choice lists leading players, then creatures, then corpses, then leftover players", "[ModuleBotSnapshotRules]")
{
    // Players 100.., creatures 200.., corpses 300..; each list already closest first.
    std::vector<TestUnit> const players = Make(100, { 1.f, 2.f, 3.f, 4.f, 5.f, 6.f });
    std::vector<TestUnit> const creatures = Make(200, { 10.f, 11.f });
    std::vector<TestUnit> const corpses = Make(300, { 20.f, 21.f });

    {
        // 4 player slots, room for everything: 4 players + 2 creatures + 2 corpses, then 2 leftover players do not fit in max 10? 10 fits all
        std::vector<TestUnit> chosen;
        Rules::ChooseNearby(players, creatures, corpses, chosen, 10, 4, CloserFirst);
        REQUIRE(chosen.size() == 10u);
        for (std::size_t i = 1; i < chosen.size(); ++i)
            CHECK(chosen[i - 1].Dist <= chosen[i].Dist);
    }
    {
        // limit 6: players 100..103, creatures 200,201 -> full; no corpses, no leftover players.
        std::vector<TestUnit> chosen;
        Rules::ChooseNearby(players, creatures, corpses, chosen, 6, 4, CloserFirst);
        REQUIRE(chosen.size() == 6u);
        int const expected[6] = { 100, 101, 102, 103, 200, 201 };
        for (std::size_t i = 0; i < 6; ++i)
            CHECK(chosen[i].Id == expected[i]);
    }
    {
        // limit 7: one corpse takes the last slot.
        std::vector<TestUnit> chosen;
        Rules::ChooseNearby(players, creatures, corpses, chosen, 7, 4, CloserFirst);
        REQUIRE(chosen.size() == 7u);
        CHECK(chosen[6].Id == 300);
    }
    {
        // limit 3: the player slots are capped by the limit.
        std::vector<TestUnit> chosen;
        Rules::ChooseNearby(players, creatures, corpses, chosen, 3, 4, CloserFirst);
        REQUIRE(chosen.size() == 3u);
        CHECK(chosen[0].Id == 100);
        CHECK(chosen[1].Id == 101);
        CHECK(chosen[2].Id == 102);
    }
    {
        // No creatures or corpses: leftover players fill the room after the leading four.
        std::vector<TestUnit> chosen;
        std::vector<TestUnit> const none;
        Rules::ChooseNearby(players, none, none, chosen, 5, 4, CloserFirst);
        REQUIRE(chosen.size() == 5u);
        CHECK(chosen[4].Id == 104);
    }
    {
        // Limit 0 chooses nothing.
        std::vector<TestUnit> chosen;
        Rules::ChooseNearby(players, creatures, corpses, chosen, 0, 4, CloserFirst);
        CHECK(chosen.empty());
    }
}

TEST_CASE("Nearby choice result is sorted by distance across categories", "[ModuleBotSnapshotRules]")
{
    std::vector<TestUnit> const players = Make(100, { 8.f, 9.f });
    std::vector<TestUnit> const creatures = Make(200, { 1.f, 12.f });
    std::vector<TestUnit> const corpses = Make(300, { 3.f });
    std::vector<TestUnit> chosen;
    Rules::ChooseNearby(players, creatures, corpses, chosen, 10, 4, CloserFirst);
    REQUIRE(chosen.size() == 5u);
    int const expected[5] = { 200, 300, 100, 101, 201 };
    for (std::size_t i = 0; i < 5; ++i)
        CHECK(chosen[i].Id == expected[i]);
}

TEST_CASE("Aura remaining time and stacks follow the snapshot conventions", "[ModuleBotSnapshotRules]")
{
    CHECK(Rules::AuraRemainingMs(true, 0) == std::numeric_limits<uint32>::max());
    CHECK(Rules::AuraRemainingMs(true, 5000) == std::numeric_limits<uint32>::max());
    CHECK(Rules::AuraRemainingMs(false, 1) == 1u);
    CHECK(Rules::AuraRemainingMs(false, 5000) == 5000u);
    CHECK(Rules::kInfiniteAuraMs != 0u);

    CHECK(Rules::AuraStacks(0) == 1u);
    CHECK(Rules::AuraStacks(1) == 1u);
    CHECK(Rules::AuraStacks(20) == 20u);
}

TEST_CASE("Aura mask bits are frozen with layout 13", "[ModuleBotSnapshotRules]")
{
    CHECK(uint32(Rules::kAuraMaskPeriodicDamage) == 0x01u);
    CHECK(uint32(Rules::kAuraMaskPeriodicHeal) == 0x02u);
    CHECK(uint32(Rules::kAuraMaskPoison) == 0x04u);
    CHECK(uint32(Rules::kAuraMaskCrowdControl) == 0x08u);
    CHECK(uint32(Rules::kAuraMaskRoot) == 0x10u);
    CHECK(uint32(Rules::kAuraMaskSlow) == 0x20u);
    CHECK(uint32(Rules::kAuraMaskInvisibility) == 0x40u);
    CHECK(uint32(Rules::kAuraMaskAbsorb) == 0x80u);
}

TEST_CASE("Aura mask is built from the spell traits", "[ModuleBotSnapshotRules]")
{
    CHECK(Rules::BuildAuraMask({}) == 0u);

    { Rules::AuraTraits t; t.PeriodicDamage = true; CHECK(Rules::BuildAuraMask(t) == 0x01u); }
    { Rules::AuraTraits t; t.PeriodicHeal = true; CHECK(Rules::BuildAuraMask(t) == 0x02u); }
    { Rules::AuraTraits t; t.PoisonOrDisease = true; CHECK(Rules::BuildAuraMask(t) == 0x04u); }
    { Rules::AuraTraits t; t.Stun = true; CHECK(Rules::BuildAuraMask(t) == 0x08u); }
    { Rules::AuraTraits t; t.Fear = true; CHECK(Rules::BuildAuraMask(t) == 0x08u); }
    { Rules::AuraTraits t; t.Confuse = true; CHECK(Rules::BuildAuraMask(t) == 0x08u); }
    { Rules::AuraTraits t; t.Stun = true; t.Fear = true; t.Confuse = true; CHECK(Rules::BuildAuraMask(t) == 0x08u); }
    { Rules::AuraTraits t; t.RootOrSnareMechanic = true; CHECK(Rules::BuildAuraMask(t) == 0x10u); }
    { Rules::AuraTraits t; t.Slow = true; CHECK(Rules::BuildAuraMask(t) == 0x20u); }
    { Rules::AuraTraits t; t.Invisibility = true; CHECK(Rules::BuildAuraMask(t) == 0x40u); }
    { Rules::AuraTraits t; t.SchoolAbsorb = true; CHECK(Rules::BuildAuraMask(t) == 0x80u); }
    { Rules::AuraTraits t; t.DamageShield = true; CHECK(Rules::BuildAuraMask(t) == 0x80u); }
    { Rules::AuraTraits t; t.PeriodicDamage = true; t.Slow = true; CHECK(Rules::BuildAuraMask(t) == 0x21u); }
    {
        Rules::AuraTraits t;
        t.PeriodicDamage = t.PeriodicHeal = t.PoisonOrDisease = t.Stun = t.RootOrSnareMechanic = t.Slow = t.Invisibility = t.SchoolAbsorb = true;
        CHECK(Rules::BuildAuraMask(t) == 0xFFu);
    }
}

TEST_CASE("SpellIdCache works a value out once per spell id", "[ModuleBotSnapshotRules]")
{
    Rules::SpellIdCache<uint32> cache;
    int calls = 0;
    int a = 0;
    auto compute = [&calls](uint32 value) { return [&calls, value] { ++calls; return value; }; };

    CHECK(cache.Get(10, &a, compute(7)) == 7u);
    CHECK(calls == 1);
    CHECK(cache.Get(10, &a, compute(99)) == 7u); // served from the cache, compute not called
    CHECK(calls == 1);
    CHECK(cache.Get(11, &a, compute(5)) == 5u);
    CHECK(calls == 2);
    CHECK(cache.Size() == 2u);
}

TEST_CASE("SpellIdCache works a value out again when the tag changes", "[ModuleBotSnapshotRules]")
{
    Rules::SpellIdCache<uint32> cache;
    int first = 0;
    int second = 0;
    int calls = 0;
    CHECK(cache.Get(1, &first, [&calls] { ++calls; return 3u; }) == 3u);
    CHECK(cache.Get(1, &second, [&calls] { ++calls; return 4u; }) == 4u);
    CHECK(calls == 2);
    CHECK(cache.Get(1, &second, [&calls] { ++calls; return 9u; }) == 4u);
    CHECK(calls == 2);
    CHECK(cache.Size() == 1u);
}

TEST_CASE("SpellIdCache remembers a null result", "[ModuleBotSnapshotRules]")
{
    Rules::SpellIdCache<int const*> cache;
    int calls = 0;
    CHECK(cache.Get(5, nullptr, [&calls]() -> int const* { ++calls; return nullptr; }) == nullptr);
    CHECK(cache.Get(5, nullptr, [&calls]() -> int const* { ++calls; return nullptr; }) == nullptr);
    CHECK(calls == 1);
}

TEST_CASE("Engaged units come first and each part is nearest first", "[ModuleBotSnapshotRules]")
{
    // value = distance; units 5, 9 and 30 are engaged
    std::vector<int> units{ 2, 5, 3, 9, 30, 1, 4 };
    auto isEngaged = [](int v) { return v == 5 || v == 9 || v == 30; };
    ModuleBotSnapshotRules::EngagedFirst(units, 3, std::less<int>(), isEngaged);
    REQUIRE(units.size() == 7);
    CHECK(units[0] == 5);
    CHECK(units[1] == 9);
    CHECK(units[2] == 30);
    // the quiet part: its nearest three come first, in order
    CHECK(units[3] == 1);
    CHECK(units[4] == 2);
    CHECK(units[5] == 3);
}

TEST_CASE("Engaged units beyond the limit still lead the list", "[ModuleBotSnapshotRules]")
{
    std::vector<int> units{ 50, 40, 1, 2, 60 };
    auto isEngaged = [](int v) { return v >= 40; };
    ModuleBotSnapshotRules::EngagedFirst(units, 2, std::less<int>(), isEngaged);
    CHECK(units[0] == 40);
    CHECK(units[1] == 50);
    CHECK(units[2] == 60);
}

// Layout 25: the party view, telegraphs and probes.

TEST_CASE("Party role bits come from the LFG roles, the main tank flag, then the talent role", "[ModuleBotSnapshotRules]")
{
    using ModuleBotSnapshotRules::PartyRoleBits;
    CHECK(PartyRoleBits(0x02, 0, 0) == 1);                 // LFG tank
    CHECK(PartyRoleBits(0x04, 0, 0) == 2);                 // LFG healer
    CHECK(PartyRoleBits(0x08, 0, 0) == 4);                 // LFG damage
    CHECK(PartyRoleBits(0x0C, 0, 0) == 6);                 // healer and damage both offered
    CHECK(PartyRoleBits(0x00, 0x02, 0) == 1);              // the main tank flag alone
    CHECK(PartyRoleBits(0x04, 0x02, 0) == 3);              // flagged main tank who queued as a healer: both
    CHECK(PartyRoleBits(0x00, 0x05, 0) == 0);              // assistant and main assist say nothing about the role
    CHECK(PartyRoleBits(0x00, 0, 1) == 1);                 // a bot's talent role fills the gap
    CHECK(PartyRoleBits(0x00, 0, 2) == 2);
    CHECK(PartyRoleBits(0x00, 0, 3) == 4);
    CHECK(PartyRoleBits(0x08, 0, 1) == 4);                 // ... but never overrides what the group says
    CHECK(PartyRoleBits(0x00, 0, 9) == 0);                 // an unknown talent role is no role
    CHECK((ModuleBotSnapshotRules::IsPartyTank(1) && ModuleBotSnapshotRules::IsPartyTank(3) && !ModuleBotSnapshotRules::IsPartyTank(6)));
}

TEST_CASE("Party debuffs list the dispellable ones first, then the longest remaining", "[ModuleBotSnapshotRules]")
{
    using Fact = ModuleBotSnapshotRules::PartyDebuffFact;
    std::vector<Fact> facts{ { 10, 5000, 0, 1 }, { 11, 2000, 4, 1 }, { 12, 9000, 0, 2 }, { 13, 8000, 1, 1 }, { 14, 2000, 2, 1 } };
    ModuleBotSnapshotRules::SortPartyDebuffs(facts);
    REQUIRE(facts.size() == 5);
    CHECK(facts[0].SpellId == 13);   // magic, 8 s
    CHECK(facts[1].SpellId == 11);   // poison, 2 s (before the curse of 2 s: stable order)
    CHECK(facts[2].SpellId == 14);   // curse, 2 s
    CHECK(facts[3].SpellId == 12);   // not dispellable, 9 s
    CHECK(facts[4].SpellId == 10);   // not dispellable, 5 s
}

TEST_CASE("Loose mobs list the ones on healers first, then the newest", "[ModuleBotSnapshotRules]")
{
    using Fact = ModuleBotSnapshotRules::LooseMobFact;
    std::vector<Fact> facts;
    facts.push_back({ 1, 100, false, 5000, 0 });
    facts.push_back({ 2, 101, true, 7000, 0 });
    facts.push_back({ 3, 102, false, 200, 0 });
    facts.push_back({ 4, 101, true, 100, 0 });
    ModuleBotSnapshotRules::SortLooseMobs(facts);
    REQUIRE(facts.size() == 4);
    CHECK(facts[0].UnitRef == 4);    // on a healer, 0.1 s
    CHECK(facts[1].UnitRef == 2);    // on a healer, 7 s
    CHECK(facts[2].UnitRef == 3);    // not on a healer, newest
    CHECK(facts[3].UnitRef == 1);
}

TEST_CASE("An idle group's view is rebuilt one tick in N, an active one every tick", "[ModuleBotSnapshotRules]")
{
    using ModuleBotSnapshotRules::PartyViewDue;
    // group 7, idle, every 5 ticks: the ticks where (tick + 7) % 5 == 0
    int due = 0;
    for (uint64 tick = 1; tick <= 20; ++tick)
        if (PartyViewDue(false, tick, 7, 5))
            ++due;
    CHECK(due == 4);
    CHECK(PartyViewDue(false, 3, 7, 5));
    CHECK(!PartyViewDue(false, 4, 7, 5));
    // two idle groups are staggered: not due on the same ticks
    CHECK(PartyViewDue(false, 3, 7, 5) != PartyViewDue(false, 3, 8, 5));
    // active: always; idleEvery 1 or 0: always
    CHECK(PartyViewDue(true, 4, 7, 5));
    CHECK(PartyViewDue(false, 4, 7, 1));
    CHECK(PartyViewDue(false, 4, 7, 0));
}

TEST_CASE("Telegraph kind follows the spell's shape", "[ModuleBotSnapshotRules]")
{
    using ModuleBotSnapshotRules::TelegraphKind;
    CHECK(TelegraphKind(true, false, false, false) == 2);    // a cone, whatever else
    CHECK(TelegraphKind(true, true, true, true) == 2);
    CHECK(TelegraphKind(false, true, false, true) == 1);     // a circle at the destination
    CHECK(TelegraphKind(false, true, false, false) == 0);    // ... but with no destination yet there is nothing to show
    CHECK(TelegraphKind(false, true, true, false) == 3);     // ... unless the spell also hits around the caster
    CHECK(TelegraphKind(false, false, true, false) == 3);    // a circle around the caster
    CHECK(TelegraphKind(false, false, false, true) == 0);    // a single-target cast
}

TEST_CASE("A trap game object is a ground effect only when armed", "[ModuleBotSnapshotRules]")
{
    using ModuleBotSnapshotRules::TrapReported;
    CHECK(TrapReported(true, 19628, true, true));
    CHECK(!TrapReported(false, 19628, true, true));   // not a trap type
    CHECK(!TrapReported(true, 0, true, true));        // a trap with no spell
    CHECK(!TrapReported(true, 19628, false, true));   // waiting to respawn
    CHECK(!TrapReported(true, 19628, true, false));   // triggered or spent
}

TEST_CASE("A trap's radius is half the template field, else the spell's", "[ModuleBotSnapshotRules]")
{
    using ModuleBotSnapshotRules::TrapRadius;
    CHECK(TrapRadius(10, 3.0f) == 5.0f);    // Jeklik's bomb: field 10, radius 5
    CHECK(TrapRadius(5, 0.0f) == 2.5f);     // Lava Bomb
    CHECK(TrapRadius(0, 3.5f) == 3.5f);     // no field: the spell's radius
    CHECK(TrapRadius(0, 0.0f) == 0.0f);     // neither: not known
}

TEST_CASE("Trap ground effects share the eight slots with the others", "[ModuleBotSnapshotRules]")
{
    CHECK(ModuleBotSnapshotRules::CapCount(11, 8) == 8);
    CHECK(ModuleBotSnapshotRules::CapCount(3, 8) == 3);
}

TEST_CASE("Telegraphs are ordered soonest to land first, the larger area on a tie", "[ModuleBotSnapshotRules]")
{
    using Key = ModuleBotSnapshotRules::TelegraphOrderKey;
    CHECK(ModuleBotSnapshotRules::TelegraphBefore(Key{ 500, 5.0f }, Key{ 1500, 20.0f }));
    CHECK(!ModuleBotSnapshotRules::TelegraphBefore(Key{ 1500, 20.0f }, Key{ 500, 5.0f }));
    CHECK(ModuleBotSnapshotRules::TelegraphBefore(Key{ 500, 10.0f }, Key{ 500, 5.0f }));
    CHECK(!ModuleBotSnapshotRules::TelegraphBefore(Key{ 500, 5.0f }, Key{ 500, 5.0f }));
}

TEST_CASE("The tick budget allows the limit per tick and starts over on a new tick", "[ModuleBotSnapshotRules]")
{
    ModuleBotSnapshotRules::TickBudget budget;
    CHECK(budget.Take(1000, 2));
    CHECK(budget.Take(1000, 2));
    CHECK(!budget.Take(1000, 2));
    CHECK(budget.Take(1050, 2));     // a new tick
    CHECK(!budget.Take(1050, 1));    // a lower limit mid-tick is respected as well
    CHECK(!budget.Take(1100, 0));    // limit 0 refuses everything
    // the first tick of the process (nowMs 0) counts like any other
    ModuleBotSnapshotRules::TickBudget fresh;
    CHECK(fresh.Take(0, 1));
    CHECK(!fresh.Take(0, 1));
}

TEST_CASE("Probe ring reports at most its capacity, oldest first", "[ModuleBotSnapshotRules]")
{
    CHECK(ModuleBotSnapshotRules::ProbeCount(0, 4) == 0);
    CHECK(ModuleBotSnapshotRules::ProbeCount(3, 4) == 3);
    CHECK(ModuleBotSnapshotRules::ProbeCount(9, 4) == 4);
    // the same slot arithmetic as the damage ring: after 6 probes into 4 slots, the next slot is 2 and the oldest reported is slot 2
    CHECK(ModuleBotSnapshotRules::DamageRingSlot(2, 4, 4, 0) == 2);
    CHECK(ModuleBotSnapshotRules::DamageRingSlot(2, 4, 4, 3) == 1);
}

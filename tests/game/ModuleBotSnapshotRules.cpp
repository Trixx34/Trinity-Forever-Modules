#include "ModuleBotSnapshotRules.h"
#include "tc_catch2.h"

#include <cstddef>
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

/*
 * Talent specs of the bots (protocol 18): the table a module's SpendBotTalentsAction.spec refers to, and how a talent profile is found for a
 * bot. Pure (standard library only) so it can be tested alone (Bots/tests/BotTalentSpecTest.cpp). The SDK keeps the same table in
 * include/forever_module/talent_spec.h (forever_module::kTalentSpecs); the interop test compares the two, so a change must be made in both.
 */
#ifndef BotTalentSpec_h__
#define BotTalentSpec_h__

#include <array>
#include <cstdint>
#include <string_view>

namespace BotTalentSpec
{
// Roles (SpendBotTalentsAction.role): 0 none, 1 tank, 2 healer, 3 dps.
inline constexpr std::uint8_t RoleTank = 1;
inline constexpr std::uint8_t RoleHealer = 2;
inline constexpr std::uint8_t RoleDps = 3;
inline constexpr std::uint8_t LastRole = 3;

inline constexpr std::uint8_t LastSpec = 28;

struct Info
{
    std::uint8_t Spec;       // 1 to 28, the wire value
    std::uint32_t ClassId;   // the core's class id: 1 warrior, 2 paladin, 3 hunter, 4 rogue, 5 priest, 7 shaman, 8 mage, 9 warlock, 11 druid
    char const* Key;         // the word after the class id in a profile file line; unique inside its class, not across classes
};

inline constexpr std::array<Info, LastSpec> Specs = {{
    { 1, 1, "arms" },
    { 2, 1, "fury" },
    { 3, 1, "prot" },
    { 4, 2, "holy" },
    { 5, 2, "prot" },
    { 6, 2, "ret" },
    { 7, 3, "bm" },
    { 8, 3, "mm" },
    { 9, 3, "sv" },
    { 10, 4, "assassination" },
    { 11, 4, "combat" },
    { 12, 4, "subtlety" },
    { 13, 5, "disc" },
    { 14, 5, "holy" },
    { 15, 5, "shadow" },
    { 16, 7, "ele" },
    { 17, 7, "enh" },
    { 18, 7, "resto" },
    { 19, 8, "arcane" },
    { 20, 8, "fire" },
    { 21, 8, "frost" },
    { 22, 9, "affliction" },
    { 23, 9, "demonology" },
    { 24, 9, "destruction" },
    { 25, 11, "balance" },
    { 26, 11, "feral_tank" },
    { 27, 11, "feral_cat" },
    { 28, 11, "resto" },
}};

// The table row of a spec (1 to LastSpec), or nullptr.
inline constexpr Info const* Get(std::uint32_t spec)
{
    return spec >= 1 && spec <= LastSpec ? &Specs[spec - 1] : nullptr;
}

// The spec a class's key names, or 0 when it is not one of that class's spec keys (tank, healer and dps are roles).
inline constexpr std::uint8_t FromKey(std::uint32_t classId, std::string_view key)
{
    for (Info const& info : Specs)
        if (info.ClassId == classId && key == info.Key)
            return info.Spec;
    return 0;
}

// A profile is stored under one key byte: the role (1 to 3) or SpecKeyBase + the spec (101 to 128).
inline constexpr std::uint8_t SpecKeyBase = 100;
inline constexpr std::uint8_t ProfileKeyOfSpec(std::uint8_t spec) { return std::uint8_t(SpecKeyBase + spec); }

// The profile key a file line's second word names for a class: a role word gives 1 to 3, one of the class's spec keys gives SpecKeyBase + spec,
// anything else 0 (the line is rejected).
inline constexpr std::uint8_t ProfileKeyFromName(std::uint32_t classId, std::string_view name)
{
    if (name == "tank")
        return RoleTank;
    if (name == "healer")
        return RoleHealer;
    if (name == "dps")
        return RoleDps;
    std::uint8_t const spec = FromKey(classId, name);
    return spec ? ProfileKeyOfSpec(spec) : std::uint8_t(0);
}

// The profile for a bot of `classId` with this role and spec in `profiles` (a map keyed by pair<classId, profile key>): the (class, spec) one first,
// then the (class, role) one, else profiles.end(). Spec 0 and role 0 skip their lookup, so a module that sends neither gets the greedy allocator.
template <typename Map>
typename Map::const_iterator FindProfile(Map const& profiles, std::uint32_t classId, std::uint8_t role, std::uint8_t spec)
{
    if (spec)
        if (auto const it = profiles.find({ classId, ProfileKeyOfSpec(spec) }); it != profiles.end())
            return it;
    if (role)
        if (auto const it = profiles.find({ classId, role }); it != profiles.end())
            return it;
    return profiles.end();
}

// The key as a word for logs: the role word or the spec key, "none" for 0.
inline char const* ProfileKeyName(std::uint8_t key)
{
    switch (key)
    {
        case RoleTank: return "tank";
        case RoleHealer: return "healer";
        case RoleDps: return "dps";
        default:
            if (key > SpecKeyBase && key <= SpecKeyBase + LastSpec)
                return Specs[key - SpecKeyBase - 1].Key;
            return "none";
    }
}
}

#endif // BotTalentSpec_h__

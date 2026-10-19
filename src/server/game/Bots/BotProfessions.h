/*
 * Professions for bots. A bot takes only combinations of primary professions that make sense (the owner's rules, 2026-10-19):
 *
 *   - two gathering professions (Mining, Herbalism, Skinning), or
 *   - one gathering and one crafting profession that go together, or two crafting professions that do:
 *       Mining + Engineering, Mining + Blacksmithing, Skinning + Leatherworking, Alchemy + Herbalism, Tailoring + Enchanting.
 *
 * Each bot is given one of the eight sets, chosen from its character guid so that it never changes, and is taught those two professions for free (like
 * its class spells: BotMgr::OnBotLevelChanged). A profession trainer's spells are never taught to a bot for a profession outside its set. Secondary
 * professions (First Aid, Cooking, Fishing, Riding) are not restricted. Alts keep the player's choices and are never touched.
 *
 * The rules below are pure (ids and numbers), checked at compile time at the end of this file. Settings: Bot.Professions.Enabled (default 1).
 */

#ifndef TRINITY_BOT_PROFESSIONS_H
#define TRINITY_BOT_PROFESSIONS_H

#include "Define.h"
#include "SharedDefines.h"

#include <array>

class Player;
namespace Trainer { class Trainer; }

namespace BotProfessions
{
inline constexpr std::array<uint32, 9> kPrimary = { SKILL_ALCHEMY, SKILL_BLACKSMITHING, SKILL_ENCHANTING, SKILL_ENGINEERING, SKILL_HERBALISM,
                                                    SKILL_LEATHERWORKING, SKILL_MINING, SKILL_SKINNING, SKILL_TAILORING };

constexpr bool IsPrimary(uint32 skill)
{
    for (uint32 primary : kPrimary)
        if (primary == skill)
            return true;
    return false;
}

constexpr bool IsGathering(uint32 skill)
{
    return skill == SKILL_MINING || skill == SKILL_HERBALISM || skill == SKILL_SKINNING;
}

// The eight sets a bot may have. The order is the order of the owner's list; the first two skills of a set are its pair.
inline constexpr std::array<std::array<uint32, 2>, 8> kSets = { {
    { SKILL_MINING, SKILL_HERBALISM },          // two gathering professions
    { SKILL_MINING, SKILL_SKINNING },
    { SKILL_HERBALISM, SKILL_SKINNING },
    { SKILL_TAILORING, SKILL_ENCHANTING },      // a crafter and the profession that disenchants for it
    { SKILL_MINING, SKILL_ENGINEERING },        // a gatherer and the crafter that uses what it gathers
    { SKILL_MINING, SKILL_BLACKSMITHING },
    { SKILL_SKINNING, SKILL_LEATHERWORKING },
    { SKILL_ALCHEMY, SKILL_HERBALISM },
} };

// Whether two skills are a combination a bot may have, in either order.
constexpr bool IsAllowedPair(uint32 a, uint32 b)
{
    if (a == b || !IsPrimary(a) || !IsPrimary(b))
        return false;
    if (IsGathering(a) && IsGathering(b))
        return true;
    for (auto const& set : kSets)
        if ((set[0] == a && set[1] == b) || (set[0] == b && set[1] == a))
            return true;
    return false;
}

// A well mixed 64-bit value (the splitmix64 finalizer), so consecutive guids do not take consecutive sets.
constexpr uint64 Mix(uint64 value)
{
    value += 0x9e3779b97f4a7c15ull;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
    return value ^ (value >> 31);
}

// The set a bot has, from its character guid (the counter): always the same one.
constexpr std::array<uint32, 2> SetFor(uint64 botGuid)
{
    return kSets[Mix(botGuid) % kSets.size()];
}

// Whether `skill` is one of the bot's two professions.
constexpr bool IsOneOf(uint64 botGuid, uint32 skill)
{
    std::array<uint32, 2> const set = SetFor(botGuid);
    return set[0] == skill || set[1] == skill;
}

// Whether a bot may learn from a trainer of `skill`: a secondary skill or an unknown one (0) always, a primary one only when it is the bot's.
constexpr bool MayLearn(uint64 botGuid, uint32 skill)
{
    return !IsPrimary(skill) || IsOneOf(botGuid, skill);
}

// ---- checked when the file is compiled ----
constexpr bool EverySetIsAllowed()
{
    for (auto const& set : kSets)
        if (!IsAllowedPair(set[0], set[1]))
            return false;
    return true;
}

constexpr bool NoSetRepeats()
{
    for (size_t i = 0; i < kSets.size(); ++i)
        for (size_t j = i + 1; j < kSets.size(); ++j)
            if (IsAllowedPair(kSets[i][0], kSets[i][1]) && ((kSets[i][0] == kSets[j][0] && kSets[i][1] == kSets[j][1]) ||
                                                              (kSets[i][0] == kSets[j][1] && kSets[i][1] == kSets[j][0])))
                return false;
    return true;
}

constexpr bool EveryPrimaryIsInASet()
{
    for (uint32 skill : kPrimary)
    {
        bool found = false;
        for (auto const& set : kSets)
            found = found || set[0] == skill || set[1] == skill;
        if (!found)
            return false;
    }
    return true;
}

static_assert(EverySetIsAllowed(), "every set of professions is one the rules allow");
static_assert(NoSetRepeats(), "no set is listed twice");
static_assert(EveryPrimaryIsInASet(), "every primary profession can be had");
static_assert(IsAllowedPair(SKILL_MINING, SKILL_SKINNING) && IsAllowedPair(SKILL_HERBALISM, SKILL_MINING), "any two gathering professions go together");
static_assert(IsAllowedPair(SKILL_ENCHANTING, SKILL_TAILORING) && IsAllowedPair(SKILL_ENGINEERING, SKILL_MINING) && IsAllowedPair(SKILL_LEATHERWORKING, SKILL_SKINNING),
    "the pairs go together in either order");
static_assert(!IsAllowedPair(SKILL_ALCHEMY, SKILL_BLACKSMITHING) && !IsAllowedPair(SKILL_ENGINEERING, SKILL_TAILORING) && !IsAllowedPair(SKILL_MINING, SKILL_MINING) &&
    !IsAllowedPair(SKILL_MINING, SKILL_COOKING) && !IsAllowedPair(SKILL_HERBALISM, SKILL_LEATHERWORKING) && !IsAllowedPair(SKILL_SKINNING, SKILL_ALCHEMY),
    "the others do not");
static_assert(MayLearn(1, SKILL_COOKING) && MayLearn(1, 0) && MayLearn(7, SKILL_FISHING), "secondary professions are not restricted");

bool Enabled();

// The profession a tradeskill trainer teaches (a SKILL_* of a primary profession), or 0 when it teaches none (cooking, first aid, fishing, riding).
uint32 SkillOfTrainer(Trainer::Trainer const& trainer);

// Whether a bot may learn every spell of this trainer, whatever its type: false as soon as one of them belongs to a primary profession the bot
// does not have. Always true when Bot.Professions.Enabled is off. Every path that teaches a bot a trainer's spells checks this.
bool TrainerMayTeach(Trainer::Trainer const& trainer, uint64 botGuid);

// Teaches a bot its two professions: every spell of their trainers that it may learn now (the first rank at once, higher ranks as its skill allows). Returns
// how many spells were taught. Not for alts.
uint32 Teach(Player& bot);
}

#endif // TRINITY_BOT_PROFESSIONS_H

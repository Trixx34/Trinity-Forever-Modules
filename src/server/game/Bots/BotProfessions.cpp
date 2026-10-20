#include "BotProfessions.h"
#include "Config.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Trainer.h"

#include <unordered_map>

namespace BotProfessions
{
bool Enabled()
{
    static bool const value = sConfigMgr->GetBoolDefault("Bot.Professions.Enabled", true);
    return value;
}

namespace
{
// The profession a spell belongs to (a primary one, or First Aid or Cooking, which bots are taught too): it has a SKILL effect for it, or it teaches
// (SPELL_EFFECT_LEARN_SPELL) a spell that has.
uint32 SkillOfSpell(uint32 spellId, uint32 depth = 0)
{
    SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE);
    if (!info)
        return 0;
    for (SpellEffectInfo const& effect : info->GetEffects())
    {
        if (effect.IsEffect(SPELL_EFFECT_SKILL) && (IsPrimary(uint32(effect.MiscValue)) || IsCraftedSecondary(uint32(effect.MiscValue))))
            return uint32(effect.MiscValue);
        if (depth == 0 && effect.IsEffect(SPELL_EFFECT_LEARN_SPELL) && effect.TriggerSpell)
            if (uint32 const inner = SkillOfSpell(effect.TriggerSpell, 1))
                return inner;
    }
    return 0;
}
}

uint32 SkillOfTrainer(Trainer::Trainer const& trainer)
{
    // Worked out once per trainer: the first of its spells that belongs to a primary profession names it.
    static std::unordered_map<Trainer::Trainer const*, uint32> known;
    auto const found = known.find(&trainer);
    if (found != known.end())
        return found->second;

    uint32 skill = 0;
    if (trainer.GetType() == Trainer::Type::Tradeskill)
        for (Trainer::Spell const& spell : trainer.Spells())
            if ((skill = SkillOfSpell(spell.SpellId)) != 0)
                break;
    known.emplace(&trainer, skill);
    return skill;
}

bool TrainerMayTeach(Trainer::Trainer const& trainer, uint64 botGuid)
{
    if (!Enabled())
        return true;
    for (Trainer::Spell const& spell : trainer.Spells())
        if (!MayLearn(botGuid, SkillOfSpell(spell.SpellId)))
            return false;
    return true;
}

uint32 Teach(Player& bot)
{
    if (!Enabled())
        return 0;

    uint64 const key = bot.GetGUID().GetCounter();
    uint32 taught = 0;
    for (int pass = 0; pass < 4; ++pass) // learning a rank can make the next one available
    {
        uint32 learned = 0;
        for (auto const& [id, trainer] : sObjectMgr->GetTrainers())
        {
            if (trainer.GetType() != Trainer::Type::Tradeskill)
                continue;
            uint32 const skill = SkillOfTrainer(trainer);
            if (skill && (IsOneOf(key, skill) || IsCraftedSecondary(skill)) && TrainerMayTeach(trainer, key))
                learned += trainer.TeachAvailableSpellsFree(&bot, true);
        }
        taught += learned;
        if (!learned)
            break;
    }
    return taught;
}
}

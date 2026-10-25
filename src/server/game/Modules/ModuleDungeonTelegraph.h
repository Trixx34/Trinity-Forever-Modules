/* Optional sim-only Deadmines combat trace. */
#ifndef TRINITY_MODULE_DUNGEON_TELEGRAPH_H
#define TRINITY_MODULE_DUNGEON_TELEGRAPH_H

#include "Config.h"
#include "Creature.h"
#include "GameTime.h"
#include "Log.h"
#include "Map.h"
#include "Player.h"
#include "SpellInfo.h"
#include "Unit.h"
#include "WorldSession.h"

#include <algorithm>
#include <atomic>
#include <string>

namespace ModuleDungeonTelegraph
{
inline bool Enabled()
{
    static bool const enabled = sConfigMgr->GetBoolDefault("Modules.Tier2.DungeonTelegraph", false);
    return enabled;
}

inline std::uint64_t NextSequence()
{
    static std::atomic<std::uint64_t> sequence{ 0 };
    return ++sequence;
}

inline bool InDeadmines(Unit const* unit)
{
    return unit && unit->GetMapId() == 36;
}

inline bool IsBot(Unit const* unit)
{
    Player const* player = unit ? unit->ToPlayer() : nullptr;
    return player && player->GetSession() && player->GetSession()->IsBot();
}

inline std::uint64_t Guid(Unit const* unit)
{
    return unit ? unit->GetGUID().GetCounter() : 0;
}

inline std::uint32_t Entry(Unit const* unit)
{
    Creature const* creature = unit ? unit->ToCreature() : nullptr;
    return creature ? creature->GetEntry() : 0;
}

inline std::uint32_t Instance(Unit const* unit)
{
    return unit && unit->GetMap() ? unit->GetMap()->GetInstanceId() : 0;
}

inline std::string Name(Unit const* unit)
{
    return unit ? unit->GetName() : "none";
}

inline void Cast(Unit const* caster, Unit const* target, SpellInfo const* spell, bool triggered)
{
    if (!Enabled() || !spell || !InDeadmines(caster))
        return;
    TC_LOG_INFO("server.worldserver",
        "dungeon_telegraph seq={} time_ms={} event=cast_start instance={} caster={} caster_name={} caster_entry={} target={} target_name={} target_entry={} spell={} triggered={}",
        NextSequence(), GameTime::GetGameTimeMS(), Instance(caster), Guid(caster), Name(caster), Entry(caster),
        Guid(target), Name(target), Entry(target), spell->Id, triggered);
}

inline void Damage(Unit const* attacker, Unit const* victim, SpellInfo const* spell, std::uint32_t amount, std::uint32_t healthBefore)
{
    if (!Enabled() || !InDeadmines(victim) || (!IsBot(attacker) && !IsBot(victim)))
        return;
    std::uint32_t const effective = std::min(amount, healthBefore);
    TC_LOG_INFO("server.worldserver",
        "dungeon_telegraph seq={} time_ms={} event=damage instance={} attacker={} attacker_name={} attacker_entry={} victim={} victim_name={} victim_entry={} spell={} amount={} effective={} victim_hp_before={}",
        NextSequence(), GameTime::GetGameTimeMS(), Instance(victim), Guid(attacker), Name(attacker), Entry(attacker),
        Guid(victim), Name(victim), Entry(victim), spell ? spell->Id : 0, amount, effective, healthBefore);
}

inline void Heal(Unit const* healer, Unit const* target, SpellInfo const* spell, std::uint32_t attempted,
                 std::uint32_t effective, std::uint32_t healthBefore)
{
    if (!Enabled() || !InDeadmines(target) || (!IsBot(healer) && !IsBot(target)))
        return;
    TC_LOG_INFO("server.worldserver",
        "dungeon_telegraph seq={} time_ms={} event=heal instance={} healer={} healer_name={} healer_entry={} target={} target_name={} target_entry={} spell={} attempted={} effective={} target_hp_before={}",
        NextSequence(), GameTime::GetGameTimeMS(), Instance(target), Guid(healer), Name(healer), Entry(healer),
        Guid(target), Name(target), Entry(target), spell ? spell->Id : 0, attempted, effective, healthBefore);
}
} // namespace ModuleDungeonTelegraph

#endif

/*
 * Layout 25: the party view. See ModuleBotPartyView.h.
 */

#include "ModuleBotPartyView.h"
#include "ModuleBotSnapshot.h"
#include "ModuleBotSnapshotRules.h"
#include "BotMgr.h"
#include "Config.h"
#include "GameTime.h"
#include "Group.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectGuid.h"
#include "Pet.h"
#include "Player.h"
#include "SharedDefines.h"
#include "Spell.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "Unit.h"
#include "WorldSession.h"

#include <algorithm>
#include <iterator>
#include <unordered_map>
#include <vector>

using namespace forever_module;

namespace
{

// A player's unit reference from its guid (the member need not be online).
uint64 PlayerRefOf(ObjectGuid guid)
{
    if (guid.IsEmpty() || guid.GetCounter() > kUnitRefCounterMask)
        return 0;
    return MakeUnitRef(uint8(HighGuid::Player), 0, guid.GetCounter());
}

uint64 RefOf(Unit const* unit)
{
    uint64 ref = 0;
    if (unit && EncodeUnitRef(*unit, ref))
        return ref;
    return 0;
}

// A raid mark's guid as a unit reference: the same shapes EncodeUnitRef packs, from the guid alone (the unit may be out of the world).
uint64 RefOfGuid(ObjectGuid guid)
{
    if (guid.IsEmpty() || guid.GetCounter() > kUnitRefCounterMask)
        return 0;
    switch (guid.GetHigh())
    {
        case HighGuid::Player:
            return MakeUnitRef(uint8(HighGuid::Player), 0, guid.GetCounter());
        case HighGuid::Pet:
        case HighGuid::Creature:
            if (guid.GetEntry() > kUnitRefEntryMask)
                return 0;
            return MakeUnitRef(uint8(guid.GetHigh()), guid.GetEntry(), guid.GetCounter());
        default:
            return 0;
    }
}

// PartyDebuff::dispel_type from the spell's DispelType.
uint8 DispelTypeOf(SpellInfo const* info)
{
    if (!info)
        return 0;
    switch (info->Dispel)
    {
        case DISPEL_MAGIC: return 1;
        case DISPEL_CURSE: return 2;
        case DISPEL_DISEASE: return 3;
        case DISPEL_POISON: return 4;
        default: return 0;
    }
}

// The harmful auras on a member, the dispellable ones first, then the longest remaining first (ModuleBotSnapshotRules::SortPartyDebuffs).
void FillDebuffs(Player const& member, PartyMember& out)
{
    std::vector<ModuleBotSnapshotRules::PartyDebuffFact> facts;
    for (auto const& [spellId, application] : member.GetAppliedAuras())
    {
        Aura const* aura = application->GetBase();
        if (!aura || application->IsPositive())
            continue;
        bool const permanent = aura->IsPermanent();
        int32 const left = aura->GetDuration();
        if (!permanent && left <= 0)
            continue;
        ModuleBotSnapshotRules::PartyDebuffFact fact;
        fact.SpellId = spellId;
        fact.RemainingMs = ModuleBotSnapshotRules::AuraRemainingMs(permanent, left);
        fact.DispelType = DispelTypeOf(aura->GetSpellInfo());
        fact.Stacks = uint8(std::min<uint32>(255, ModuleBotSnapshotRules::AuraStacks(aura->GetStackAmount())));
        facts.push_back(fact);
    }
    ModuleBotSnapshotRules::SortPartyDebuffs(facts);
    std::size_t const count = ModuleBotSnapshotRules::CapCount(facts.size(), kMaxPartyDebuffs);
    for (std::size_t i = 0; i < count; ++i)
    {
        out.debuffs[i].spell_id = facts[i].SpellId;
        out.debuffs[i].remaining_ms = facts[i].RemainingMs;
        out.debuffs[i].dispel_type = facts[i].DispelType;
        out.debuffs[i].stacks = facts[i].Stacks;
    }
    out.debuff_count = uint8(count);
}

// The facts of a member that is in the world: position, health, power, life, combat, cast, pet, transport, attackers, debuffs.
void FillOnlineMember(Player& member, PartyMember& out, bool threat)
{
    out.online = 1;
    out.is_bot = member.GetSession() && member.GetSession()->IsBot() ? 1 : 0;
    out.pos_x = member.GetPositionX();
    out.pos_y = member.GetPositionY();
    out.pos_z = member.GetPositionZ();
    out.map_id = member.GetMapId();
    out.health = ModuleBotSnapshotRules::ClampToUint32(member.GetHealth());
    out.max_health = ModuleBotSnapshotRules::ClampToUint32(member.GetMaxHealth());
    Powers const powerType = member.GetPowerType();
    out.power_type = uint8(powerType);
    out.power = ModuleBotSnapshotRules::ClampToUint32(int64(member.GetPower(powerType)));
    out.max_power = ModuleBotSnapshotRules::ClampToUint32(int64(member.GetMaxPower(powerType)));
    out.life_state = member.IsAlive() ? 0 : (member.HasPlayerFlag(PLAYER_FLAGS_GHOST) ? 2 : 1);
    out.rez_pending = member.IsResurrectRequested() ? 1 : 0;
    out.in_combat = member.IsInCombat() ? 1 : 0;
    out.victim_ref = RefOf(member.GetVictim());

    // The cast in progress and whom it is aimed at (0 = the member itself), as BuildSnapshotBot reports the bot's own.
    for (CurrentSpellTypes type : { CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL })
    {
        Spell const* spell = member.GetCurrentSpell(type);
        if (!spell || spell->getState() == SPELL_STATE_FINISHED || !spell->GetSpellInfo())
            continue;
        out.casting_spell = spell->GetSpellInfo()->Id;
        out.cast_remaining_ms = uint32(std::max<int32>(0, spell->GetTimer()));
        if (ObjectGuid const target = spell->GetUnitTargetGUID(); !target.IsEmpty() && target != member.GetGUID())
            if (Unit const* aimed = ObjectAccessor::GetUnit(member, target))
                out.casting_target = RefOf(aimed);
        break;
    }

    if (Pet* pet = member.GetPet(); pet && pet->IsInWorld())
    {
        out.pet_guid = RefOf(pet);
        out.pet_health = ModuleBotSnapshotRules::ClampToUint32(pet->GetHealth());
        out.pet_max_health = ModuleBotSnapshotRules::ClampToUint32(pet->GetMaxHealth());
    }
    if (!member.GetTransGUID().IsEmpty())
        out.transport_entry = member.GetTransGUID().GetEntry();
    if (threat)
        out.attacker_count = uint8(std::min<std::size_t>(member.getAttackers().size(), 255));
    FillDebuffs(member, out);
}

// Rebuilds one group's view and member list into its watch entry (generation + 1).
void RebuildView(Group const& group, uint64 groupId, bool threat, uint32 now, PartyViewWatch& memory)
{
    PartyView& view = memory.View;
    uint32 const generation = view.generation + 1;
    view = PartyView{};
    view.group_id = groupId;
    view.generation = generation;
    ObjectGuid const leader = group.GetLeaderGUID();
    view.leader_ref = PlayerRefOf(leader);
    view.is_raid = group.isRaidGroup() ? 1 : 0;
    ObjectGuid const* icons = group.GetTargetIcons();
    for (std::size_t i = 0; i < TARGET_ICONS_COUNT && i < kMaxRaidMarks; ++i)
        view.raid_marks[i] = RefOfGuid(icons[i]);

    memory.Members.clear();
    memory.Instance = false;
    std::vector<ModuleBotSnapshotRules::LooseMobFact> loose;
    uint32 engaged = 0;
    uint64 firstTank = 0;
    for (Group::MemberSlot const& slot : group.GetMemberSlots())
    {
        if (memory.Members.size() >= kMaxPartyMembers)
            break;
        PartyMember entry;
        entry.guid = PlayerRefOf(slot.guid);
        entry.class_id = slot._class;
        entry.subgroup = slot.group;
        entry.member_flags = uint8((slot.flags & (MEMBER_FLAG_ASSISTANT | MEMBER_FLAG_MAINTANK | MEMBER_FLAG_MAINASSIST)) |
                                   (slot.guid == leader ? uint8(kPartyMemberLeader) : uint8(0)));
        Player* member = ObjectAccessor::FindConnectedPlayer(slot.guid);
        bool const isBot = member && member->GetSession() && member->GetSession()->IsBot();
        entry.role = ModuleBotSnapshotRules::PartyRoleBits(slot.roles, slot.flags, isBot ? BotMgr::TalentRoleOf(slot.guid.GetCounter()) : 0);
        if ((slot.flags & MEMBER_FLAG_MAINTANK) != 0 && view.main_tank_ref == 0)
            view.main_tank_ref = entry.guid;
        if ((slot.flags & MEMBER_FLAG_MAINASSIST) != 0 && view.main_assist_ref == 0)
            view.main_assist_ref = entry.guid;
        if (firstTank == 0 && ModuleBotSnapshotRules::IsPartyTank(entry.role))
            firstTank = entry.guid;

        if (member && member->IsInWorld())
        {
            FillOnlineMember(*member, entry, threat);
            if (entry.in_combat)
                view.in_combat = 1;
            if (slot.guid == leader)
                if (Map const* map = member->FindMap(); map && (map->IsDungeon() || map->IsRaid()))
                    memory.Instance = true;
            // The creatures on this member: every one counts as engaged; one on a member who is not a tank is loose (the aggro-loss list).
            if (threat)
            {
                for (Unit* attacker : member->getAttackers())
                {
                    if (!attacker || !attacker->IsAlive() || !attacker->IsCreature())
                        continue;
                    if (engaged < 255)
                        ++engaged;
                    if (ModuleBotSnapshotRules::IsPartyTank(entry.role))
                        continue;
                    uint64 const ref = RefOf(attacker);
                    if (ref == 0)
                        continue;
                    auto const since = memory.LooseSinceMs.find(ref);
                    uint32 const start = since != memory.LooseSinceMs.end() ? since->second : now;
                    ModuleBotSnapshotRules::LooseMobFact fact;
                    fact.UnitRef = ref;
                    fact.VictimRef = entry.guid;
                    fact.VictimIsHealer = (entry.role & kPartyRoleHealer) != 0;
                    fact.SinceMs = now - start;
                    fact.StartMs = start;
                    loose.push_back(fact);
                }
            }
        }
        memory.Members.push_back(entry);
    }
    if (view.main_tank_ref == 0)
        view.main_tank_ref = firstTank;
    view.engaged_count = uint8(engaged);

    // Only the creatures loose now keep their first-seen time; the list is the most urgent first (mobs on healers, then the newest).
    std::unordered_map<uint64, uint32> stillLoose;
    for (ModuleBotSnapshotRules::LooseMobFact const& fact : loose)
        stillLoose[fact.UnitRef] = fact.StartMs;
    memory.LooseSinceMs.swap(stillLoose);
    ModuleBotSnapshotRules::SortLooseMobs(loose);
    std::size_t const count = ModuleBotSnapshotRules::CapCount(loose.size(), kMaxLooseMobs);
    for (std::size_t i = 0; i < count; ++i)
    {
        view.loose_mobs[i].unit_ref = loose[i].UnitRef;
        view.loose_mobs[i].victim_ref = loose[i].VictimRef;
        view.loose_mobs[i].since_ms = loose[i].SinceMs;
    }
    view.loose_count = uint8(count);
}

} // namespace

bool PartyViewsEnabled()
{
    static bool const enabled = sConfigMgr->GetBoolDefault("Bot.Snapshot.PartyView", false);
    return enabled;
}

void BuildPartyViews(std::vector<Player*> const& bots, uint32 maxViews, uint32 maxMembers, uint64 tick, PartyViewWatchMap& watch,
                     ModuleSnapshotParties& parties, PartyViewSlotMap& slots)
{
    static bool const threat = sConfigMgr->GetBoolDefault("Bot.Snapshot.PartyThreat", false);
    static uint32 const idleEvery = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.Snapshot.PartyViewIdleTicks", 5), 1, 100));
    uint32 const now = GameTime::GetGameTimeMS();

    parties.Views.clear();
    parties.Members.clear();
    slots.clear();
    std::unordered_map<uint64, uint32> viewOfGroup; // group counter -> index in parties.Views
    for (Player* bot : bots)
    {
        Group const* group = bot->GetGroup();
        if (!group)
            continue;
        uint64 const groupId = group->GetGUID().GetCounter();
        auto known = viewOfGroup.find(groupId);
        if (known == viewOfGroup.end())
        {
            if (parties.Views.size() >= maxViews)
                continue;
            PartyViewWatch& memory = watch[groupId];
            // Rebuilt every tick while the group fights or its leader is in an instance (as of its last build), one tick in idleEvery otherwise,
            // and whenever its membership changed.
            bool const active = memory.View.in_combat != 0 || memory.Instance;
            if (memory.LastSeenTick == 0 || memory.Members.size() != group->GetMembersCount() ||
                ModuleBotSnapshotRules::PartyViewDue(active, tick, groupId, idleEvery))
                RebuildView(*group, groupId, threat, now, memory);
            memory.LastSeenTick = tick;
            if (memory.Members.size() > std::size_t(maxMembers) - parties.Members.size())
                continue; // the pool is full: this group's bots read kNoPartyView this tick
            PartyView view = memory.View;
            view.member_first = uint32(parties.Members.size());
            view.member_count = uint32(memory.Members.size());
            parties.Members.insert(parties.Members.end(), memory.Members.begin(), memory.Members.end());
            known = viewOfGroup.emplace(groupId, uint32(parties.Views.size())).first;
            parties.Views.push_back(view);
        }
        uint32 const viewIndex = known->second;
        PartyView const& view = parties.Views[viewIndex];
        uint64 const self = PlayerRefOf(bot->GetGUID());
        uint32 memberIndex = kNoPartyView;
        for (uint32 i = 0; i < view.member_count; ++i)
            if (parties.Members[view.member_first + i].guid == self)
            {
                memberIndex = view.member_first + i;
                break;
            }
        slots[bot->GetGUID().GetCounter()] = { viewIndex, memberIndex };
    }

    // Groups no bot of this list is in any more are forgotten. Two ticks of grace: another module's region may share the watch and run after this one.
    for (auto it = watch.begin(); it != watch.end();)
        it = it->second.LastSeenTick + 2 > tick ? std::next(it) : watch.erase(it);
}

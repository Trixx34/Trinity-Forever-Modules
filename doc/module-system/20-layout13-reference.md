# 20. Tier 2 layout 13 in the core

The SDK's `include/forever_module/tier2_layout.h` is the authority for the Tier 2 layout. The core keeps a byte-identical
copy in `src/server/game/Modules/ModuleTier2Layout.h`, because the shared-memory region is mapped by both processes and any
divergence -- a field moved, a padding changed, a `static_assert` dropped -- is silent memory corruption. The two files
must be `diff`-identical; the only edits allowed in the copy are none.

This page is the other half of the contract: what the core actually *fills* in the fields layout 13 added. A field that
reads as zero is indistinguishable from a field that is not implemented, so every one of them has a producer.

## What layout 13 added, and who fills it

| Field | Filled by | Notes |
|-------|-----------|-------|
| `BotSnapshot::aura_count`, `auras[kMaxAuras]` | `ModuleBotSnapshot.cpp` (`CollectAuras`, `FillAuras`) | The bot's active auras, at most 16 |
| `NearbyUnit::aura_count`, `aura_mask` | `ModuleBotSnapshot.cpp` (`FillNearbyAuras`) | Summary only; a nearby unit gets no aura array |
| `BotSnapshot::casting_target` | `ModuleBotSnapshot.cpp`, in the cast-state loop | Unit reference, 0 for a cast on the bot itself |
| `BotSnapshot::pet_guid`, `pet_health`, `pet_max_health`, `pet_state` | `ModuleBotSnapshot.cpp` (`FillPet`) | 0 none, 1 alive, 2 dead |
| `BotSnapshot::vendor_guid`, `repair_guid`, `auctioneer_guid` | `ModuleBotSnapshot.cpp` (`FillNearestVendor`) | Unit references built from the spawn, not from a live creature |
| `BotSnapshot::copper`, `bag_free_slots` | `ModuleBotSnapshot.cpp` (`FillMoneyAndBags`) | |
| `BotSnapshot::last_target_lost_reason` | `ModuleBotSnapshot.cpp` (`ClassifyTargetLoss`, `BotSnapshotWatch`) | 0 none, 1 killed, 2 leashed, 3 despawned, 4 another player |
| `BotActionStatus::kNoRoute` (13) | `ModuleBotActions.cpp` (`HasFullRouteTo`) | Sent for `kMoveTo` only; the move is still applied |
| `ActionType::kPetCommand` (22) | `ModuleBotActions.cpp` (`PetCommandAction`) | |
| `ActionType::kCraft` (20), `kGather` (21) | `ModuleBotActions.cpp` | **Refused** with a reason: not implemented in this core |

## `AuraEntry`

- `spell_id` -- the spell, once, however many casters applied it. A second application of the same spell lengthens the
  time left instead of taking a second slot.
- `remaining_ms` -- milliseconds left, or `0xFFFFFFFF` for an aura with no duration. A module only tests it against 0
  ("is it running?"), so the sentinel does not have to be exact.
- `stacks` -- the stack count, at least 1.
- An aura the unit no longer carries is left out. The layout allows reporting one with `remaining_ms == 0` ("just
  expired"); the core does not, because a module cannot tell that apart from a stale entry.

When a unit carries more auras than the snapshot has slots for, the ones with a summary bit below (a DOT, a poison, a
stun -- the ones a module acts on) are reported first and the rest fill what is left.

## `NearbyUnit::aura_mask`

The layout calls the bits "core-defined and fixed per layout" and the SDK does not publish them. **This is the core's
assignment for layout 13.** It is frozen with the layout: a later layout may add bits, but must not move or repurpose
one, so a module that hard-codes these can trust them for as long as `kTier2LayoutVersion` is 13.

| Bit | Value | Meaning |
|-----|-------|---------|
| 0 | `0x00000001` | A DOT: `SPELL_AURA_PERIODIC_DAMAGE` |
| 1 | `0x00000002` | `SPELL_AURA_PERIODIC_HEAL` |
| 2 | `0x00000004` | A poison or a disease (a periodic damage with that mechanic) |
| 3 | `0x00000008` | Crowd control: stun, fear or confusion |
| 4 | `0x00000010` | Rooted or snared |
| 5 | `0x00000020` | `SPELL_AURA_MOD_DECREASE_SPEED` |
| 6 | `0x00000040` | `SPELL_AURA_MOD_INVISIBILITY` |
| 7 | `0x00000080` | A damage shield or an absorb |

`aura_mask` is a coarse summary on purpose: it is what lets a module tell "this target has a DOT" without the core
filling an aura array for every unit near every bot. A module that needs the spell id and the time left has to look at
the bot's own `auras[]`, which only exists for bots, not for nearby units.

## `last_target_lost_reason`

The core only classifies a loss when the bot's victim actually went away (it had a target on the previous snapshot tick
and has none now), and keeps the reason until the bot picks a new target, so a module reads it on the snapshot tick that
carries `threat_target_guid == 0`.

| Value | Meaning | How the core tells |
|-------|---------|-------------------|
| 1 | Killed | The victim is in the world and dead (a corpse counts) |
| 2 | Leashed | A creature that is out of combat and back within 1 yard of its spawn point |
| 3 | Despawned | Nothing of the victim is in the world any more, not even a corpse |
| 4 | Another player | The victim is alive and fighting a player who is not this bot |

A loss the core cannot pin down reports **0**, and a module must then read it as a plain `target_gone` and *not* avoid
the unit. That is the one safe direction: every other code tells a module to leave that unit alone for a while, and a
wrong `leashed` on a mob that is simply still there would keep a bot from ever fighting it.

## `kNoRoute`

`HasFullRouteTo` asks `PathGenerator` for the path type before the move starts:

- `PATHFIND_NOPATH` or `PATHFIND_INCOMPLETE` (with or without `PATHFIND_FARFROMPOLY`) -- no full route, so `kNoRoute`.
- `PATHFIND_NOT_USING_PATH` -- **not** a missing route. That is what `PathGenerator` returns when there is no navmesh to
  use at all (no mmaps for the map, or the unit flies, swims or falls), where a straight line is the intended movement.
  Reporting `kNoRoute` there would make every destination on such a map "unreachable", and a module that drops
  unreachable moves would stop moving its bots entirely.
- `PATHFIND_SHORT` -- **not** reported. It is a path-length cap (about 296 yards), not a reachability failure.

**The move is never refused over this.** `PointMovementGenerator` already falls back to a straight line when there is no
path, so the bot walks to the nearest reachable point and stops there, which is what the status documents. `kNoRoute`
only changes the answer the module gets. A module drops an unreachable destination on the first result it sees (it
de-duplicates on `last_action_sequence`), and the core reports it on every move action it applies to that destination --
which is once per destination the module publishes, not once per tick: the module only publishes an action when it
differs from the last one it published for that bot.

## Cost

One extra `PathGenerator::CalculatePath` per applied `kMoveTo`, because `PointMovementGenerator::Initialize` computes the
path again on the world thread when the move starts. There is no way to hand a computed path to `MovePoint`, and the two
answers would not be identical anyway (the bot has moved a little in between).

Auras: one walk of the aura container per bot per snapshot tick, and one per nearby unit per *refresh* (one tick in
`refreshEvery`, staggered by guid, not every tick for every bot). `BotSnapshot` grew from 376 to 648 bytes, so the
snapshot write for 1500 bots went from about 0.5 MiB to 0.9 MiB per buffer; the region is 4.3 MiB at the default bounds.

## Protocol

The core's `kModuleProtocolVersion` is **5**; the SDK's `kProtocolVersion` is **7**. The SDK is the authority and has
already been pushed, so this is a gap in the core, not a difference of opinion. It is a hard blocker at module load:
`ModuleMgr::CheckProtocolVersion` refuses a manifest that declares 7 before the process is even spawned.

What protocol 6 and 7 added on Tier 1, and what the core has:

| Added in | Surface | In this core |
|----------|---------|--------------|
| 6 | `BuyItemAction` (actions.proto field 18) | no |
| 6 | `AuctionHouseAction` (actions.proto field 19) | no |
| 7 | `ActionReply.copper_spent` (9), `item_entry` (10), `auction_guid` (11) | no |
| 7 | `AuctionListingEvent` via `PlayerScript.OnAuctionResult` (events.proto field 17) | no |

The core's version string was left at 5 on purpose: declaring 7 without that surface would let a protocol-7 module
attach to a core that silently refuses the actions protocol 7 promises, which is a worse failure than a refused
handshake. Closing the gap means implementing those four things and then bumping the string.

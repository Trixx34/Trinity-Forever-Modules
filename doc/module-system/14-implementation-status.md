# 14. Implementation status

The design documents (00 to 12) are the records of what was decided. Their status lines were written before the code,
so they still say "not yet implemented". This page says what exists now. Where the code differs from a design doc,
the code is the reference and the difference is listed.

## Implemented

| Area | What exists | Where |
|------|-------------|-------|
| Process host (04) | Discovery, spawn, Tier 1 handshake, heartbeat, restart policy, approval store (06) | `src/server/game/Modules/ModuleMgr.*`, `ModuleApproval.*` |
| Tier 1 transport | Length-prefixed proto2 frames over Unix sockets (AF_UNIX on Windows 10 1803+) | `ModuleSocket.*`, `ModuleSession.*`, `ModuleWire.*` |
| Events (03) | `PlayerScript.OnLootItem` notify; chat commands via `CommandScript` | `ModuleMgr::DispatchItemLooted`, `RouteCommand` |
| Module actions | `SellItem`, `SendPlayerMessage`, `ShareQuestItem` (gives a looted quest item to the party members who need it; the core finds who, within range, with a quest that needs it and room in the bags) | `ModuleActions.cpp` |
| Module config | `Module.<name>.<Key>` from `modules.d/<name>.conf`, sent in `ModuleConfig` after accept | `ModuleMgr::LoadModuleConfig` |
| Tier 2 region | Shared-memory region per module, double-buffered | `ModuleTier2.*`, `ModuleTier2Layout.h` (copy of the SDK header) |
| Tier 2 snapshots | Owned, online bots: position, map, health, power, level, combat target, the result of the last action, cooldowns, party state, the nearest vendor on the bot's map, and nearby units (creatures first; hostile means the bot may fight it). Layout 13 adds the bot's auras, the target of the cast in progress, the pet, the vendor/repair/auctioneer references, the money and free bag slots, why the bot lost its target, and a nearby unit's aura count and summary mask (doc 20) | `ModuleBotSnapshot.*`, `ModuleTier2::PublishSnapshot` |
| Tier 2 actions | `stop`, `move` (reporting `kNoRoute` when the navmesh has no full route), `follow` (players and creatures), `attack`, `cast` (spells the bot knows), `rest`, `vendor` (sell grey, repair, equip), `train`, `group` (invite, accept, leave), `loot` (a corpse, through the bot's own loot handlers), `pet command` (layout 13) | `ModuleBotActions.*`, `ModuleTier2::ReadNewActions` |
| Unit references | Type, entry and 32-bit counter in one 64-bit value | SDK `tier2_layout.h` (`MakeUnitRef`) |
| Bot control | `RequestControl`, `ReleaseControl`, `ControlRevoked`; one owner per bot; `max_bots` | `ModuleBotOwnership.*`, `ModuleMgr::ExecuteControlRequest` |
| Bot lifecycle | Create, log in and out through a queue, relogin delay; at shutdown, logins in flight are finished (bounded by the login timeout) before logout | `src/server/game/Bots/BotMgr.*` |
| Spawn level | `SpawnParams.level` honoured; 0 is the starting level; above max level refused | `BotModuleBackend::Spawn`, `BotMgr::CreateBot` |
| Spawn team | 0 from race, 1 Alliance, 2 Horde; a team that disagrees with the race is refused | `BotModuleBackend::Spawn` |
| Spell cooldowns | Up to 8 active cooldowns per bot in each snapshot, as spell id and remaining ms, closest to expiring first | `ModuleBotSnapshot.cpp` (`FillCooldowns`) |
| Module store | Per-module key/value rows in the world database, through the action path; the module's own rows only | `ModuleStore.*`, `ModuleStoreRules.*`, `sql/updates/world/master/2026_10_12_00_world.sql`, doc 15 |
| Bot limits | `Bot.Login.*`, `Bot.Limit.*`, `Bot.Run.StopTime` (clean shutdown at a local time), `Bot.Spread.*`; `Bot.Dump.Abilities` logs what each race and class has at each level | `BotMgr.*`, `worldserver.conf.dist` |
| Restart reclaim | The module keeps its bot guids as store rows (`bot/<guid>`) and takes them back on start | module repo `src/bot_store.*`; core store (doc 15) |

## Differs from the design documents

- **Tier 2 actions.** Design 11 had `PublishActions(ActionQueue const&)`. The SDK has
  `bool PublishActions(std::vector<BotAction> const&, std::string& error)`. It refuses more actions than `MaxActions`
  and any unknown action type, and publishes nothing on refusal.
- **Tier 2 targets.** Units are referenced by a 64-bit unit reference (type, entry, 32-bit counter), not a bare guid.
  Units whose counter does not fit are refused.
- **Team.** `SpawnParams.team`: 0 takes the race's faction; 1 is Alliance; 2 is Horde. Any other value, or a team that
  disagrees with the race, is refused before any account is created.
- **Spell cooldowns.** Each cooldown carries its spell id (`SpellCooldown`), so the module knows which spell it is.
- **Protocol and layout.** The Tier 2 layout is **13** (the core's `ModuleTier2Layout.h` is a byte-identical copy of the SDK header; doc 20 lists what the core fills for it). The Tier 1 `kModuleProtocolVersion` is **5** while the SDK declares **7**: a manifest declaring 7 is refused at load time, before the process is spawned. Doc 20 lists the exact Tier 1 surface that is missing and why the version string was not raised without it. Each layout change is breaking: a module built against another layout is refused at region attach.
- **Revocation and state.** A `ControlRevoked` does not remove a bot from the module's state file, because the core
  revokes all bots at shutdown. Only a `character not found` reply removes a guid.
- **Action timing.** A module publishes at most one action set per snapshot tick. The core applies the newest set it
  sees; two publishes between two core ticks leave only the second visible.

## Still open

- Config is sent once, after accept. A reload does not re-send it (design 03 says it should).
- Resource limits (`Modules.ResourceLimits.*`) are declared but not enforced.
- Core unit tests for the action and snapshot code need a build; only syntax checks have run for them.
- `kCraft` (20) and `kGather` (21) are inside the layout but refused with a reason: crafting and gathering are not
  implemented in the core. `kPetCommand` (22) is implemented (doc 20).
- Tier 1 `BuyItem` and `AuctionHouse` are now dispatched through `ModuleActions.cpp`, using the core's vendor and
  auction-house handlers; `ActionReply.copper_spent/item_entry/auction_guid` and `PlayerScript.OnAuctionResult`
  (`AuctionListingEvent`) are implemented. `kModuleProtocolVersion` is 7. Limitations: AH bid/buyout still need an
  acting player in the `AuctionHouseAction` schema; vendor `BuyItem` needs a buyer too; existing hunters get no starter
  ammo (only newly created non-alt hunters get one stack). See doc 20.
- Run on a dedicated sim worldserver with up to 1000 bots (spawn, login, the ramp, the safety valves, telemetry); sustained combat, levelling, looting, vendor and hub walks and parties are not yet verified there. See doc 13.
- The core builds on Windows only here: the changes since the first sim runs (layouts 5 to 7, the loot and share actions, PR 80) have been checked by reading, not compiled.

## Layout 22 / protocol 20 and the social actions

- `kTier2LayoutVersion` is 22 and `kModuleProtocolVersion` is 20 (`ModuleTier2Layout.h`, `ModuleMgr.h`); `tests/ModuleLayoutSync.cmake` compares them with the SDK.
- Tier 2 actions added since layout 20 that the core executes: `kRespecTalents` (29, the last action type), `kListCommodity` (auction commodity listing) and
  `BotSpeakAction` kind 2 (a line on the General or Trade channel; `SpeakChannel`).
- Tier 1 `ActionRequest` fields 23 to 26 (additive: an older core skips them and answers "unknown action"), run by `ModuleBotSocial.cpp`:
  `DuelAction` (challenge through the real duel spell, accept, decline), `BattlegroundAction` (solo queue through the real BattlemasterJoin handler, leave queue,
  leave battleground), `GuildAction` (accept, decline, leave) and `GiftMailAction` (one whole stack and/or copper by mail, postage paid). Each refuses a bot the
  module does not control, and checks its preconditions again when it runs. Settings: `Bot.Social.Enabled` (0), `Bot.Social.GiftMaxCopper` (10000).
- Not done: nothing accepts a battleground invitation for a queued bot; the duel challenge and guild invitation are not events, so a module learns of them
  only by asking (a guild accept answers "no pending guild invitation" when there is none).

## Protocol 27 (SDK 0.28): todo-core2 additions, all uncompiled until Jenkins builds them

- `kVendor` bit 2048 (`Bot.Vendor.SellGear`, default 0): sells bag gear the worn gear outclasses. Pure rule `ModuleBotGearSellRules.h` (tests in `tests/game/ModuleBotGearSellRules.cpp`), core function `SellOutclassedGear`.
  Green or lower, usable, required level reached, not quest or set, every slot of the type worn by the same inventory type with an item level at least as high and a score 30 higher
  (the score is `GearScore` plus `RoleStatExtra`: attack power, spell power and hit by the profile in bits 12-14 of `spell_id`), never heavier body armour; never by item id.
  Logs `vendor_sell_gear bot item reason`. The equip passes keep plain `GearScore`; moving them to the role weights is a separate decision (it changes what every bot wears).
- `ActionResult::reason` (an `ActionReason`, in the old reserved 16 bits, no layout bump): `QuestAction` fills it for a refused accept or start from the first failing
  `CanTakeQuest` check (`QuestTakeRefusal`, tested). `ApplyBotAction` has an optional `reasonOut`, `ModuleMgr::RecordActionResult` stores it in the ring.
- `kQuest` operation 5: start from the quest-starting item the bot carries (`AddQuestAndCheckCompletion(quest, item)` spends it) or from the game object whose entry is `target_guid` within interaction range.
- Event `PlayerScript.OnBossKill` (`BossKilledEvent`, EventNotify field 23, protocol 27 an append, base 26): dispatched from `Unit::Kill` after `OnCreatureKill` for a
  `CREATURE_FLAG_EXTRA_DUNGEON_BOSS` creature killed by a player, pet or charm, wired like `GroupScript.OnInviteDeclined`.

# World thread path searches (2026-11-15)

## Measured (prod, 495 bots, perf 30 s record + 10 s DWARF call graph, inclusive)

| Function | Share |
|---|---|
| ModuleMgr::OnWorldTick | 62% |
| BuildSnapshotBot | 59% |
| FillNearestVendor | 43% |
| PathGenerator::CalculatePath / BuildPolyPath | 50% |
| dtNavMeshQuery::findPath (+ dtNodePool::getNode 11%) | 44% |
| ModuleBotSupplies::RouteLooksOpen (from PickReachable) | 32% |
| FillSupplies | 24% |
| FillTaxi | 15% |
| HasFullRouteTo (kMoveTo, then MovePoint searches again) | 15% |
| NormalizePath / GetWaterOrGroundLevel | 9% |
| SpellIdCache lookups | 3-5% |
| FillNearestVendor own loop | 5.7% |

## Why so many searches

1. The vendor lookup is redone whenever a bot moved 100 yards (`kVendorLookupMoveYards`), and the redo resets the whole `VendorLookup`: supplies, taxi master and reagent vendor are chosen again. Each choice is up to 4 candidates x (ammo, food, goods, taxi) = up to ~16 full Detour searches per bot per 100 yards walked.
2. Nothing was shared: 495 bots walking through the same towns repeated the same (map, vendor, start area) question. Negative answers (a vendor across a river) are the worst: the search expands the whole node pool and fails, and was repeated every time.
3. `g_pathBudget` allowed 24 real searches per world update, i.e. the budget alone could cost tens of ms per update.
4. kMoveTo called `HasFullRouteTo` (a full search) and then `MovePoint(generatePath=true)` searched again, every tick a module resent the same destination.
5. Bag counts (`CountAllCarried`) every 2 s per bot.

## Changes

| Change | Where | Expected saving |
|---|---|---|
| Shared route cache keyed (map, vendor spawn id, 40 yd start cell), open 600 s, closed 1800 s, bounded 50000 entries (`ModuleBotRouteCache.h`) | `PickReachable` | Most of the 32% RouteLooksOpen: after warm-up a town costs a handful of searches total, not per bot. Dominant fix. |
| Per-update budget 24 -> 6 real searches (cache hits free) | `PathBudget` | Hard cap of the worst case per update; a choice that runs out waits for the next update (already the existing behaviour). |
| Far gate: vendor further than 400 yd straight line counts as open with no search | `PickReachable` | Removes the long searches that ended INCOMPLETE anyway (RouteLooksOpen already returned open for INCOMPLETE beyond 250 yd). |
| Cache bypassed while bot swims/flies/falls | same | Correctness (those paths are not navmesh paths). |
| kMoveTo route answer remembered per bot 3 s (same 2 yd destination cell and 16 yd start cell) | `HasFullRouteToCached` | Up to the 15% HasFullRouteTo, for modules that resend a destination every tick. Does not remove the MovePoint search itself. |
| Bag counts every 5 s instead of 2 s | `FillSupplies` | Part of FillSupplies / Player::HasItemCount / ForEachItem. |

Already present, not changed: SpellInfo lookups are already cached per spell id (`CachedSpellInfo`); the 3-5% there is the hash lookup per known spell per bot.

## Not done (follow-ups)

- A dedicated `dtNavMeshQuery` with a small `maxNodes` for checks, or a cheaper reachability test (e.g. connected-component ids per polygon). Needs changes in MMapManager/PathGenerator; would also make cache misses cheap. Best next step.
- Avoiding the second search in MovePoint (pass the computed path to MoveSplineInit).
- FillNearestVendor's own scan over all spawns of the map: could use a spatial grid; 5.7%.
- Gating supply choice on whether the bag needs it: the module decides need from the snapshot, so the vendor positions must stay published.

## Behaviour and risks

- kNoRoute semantics unchanged. The only visible differences: a kMoveTo answer can be up to 3 s old; a vendor answer can be up to 10 min (open) / 30 min (closed) old and is shared by bots in the same 40 yd cell.
- A bad negative answer is shared: a bot standing off the mesh in a cell could mark a vendor closed for that cell for 30 min. Lower `Bot.Perf.RouteCache.ClosedTtlSec` if it shows.
- Vendors > 400 yd away that have no navmesh route at all (NOPATH) are now offered; the walk then ends where the mesh ends. Set `Bot.Perf.RouteTrustFarYards = 0` to restore.
- Not compiled here (Jenkins builds the core). The cache header has a standalone test (`ModuleBotRouteCacheTest`, registered in `tests/ModuleStandaloneTests.cmake`) and was run locally with g++.

## New config keys (every key must go into each environment's config)

| Key | Default |
|---|---|
| Bot.Perf.SupplyPathChecksPerUpdate | 6 |
| Bot.Perf.RouteCache.OpenTtlSec | 600 |
| Bot.Perf.RouteCache.ClosedTtlSec | 1800 |
| Bot.Perf.RouteCache.CellYards | 40 |
| Bot.Perf.RouteCache.MaxEntries | 50000 |
| Bot.Perf.RouteTrustFarYards | 400 |
| Bot.Perf.SupplyCountEveryMs | 5000 |
| Bot.Perf.MoveToRouteTtlMs | 3000 |

No SQL changes.

## Measuring after deploy

```
perf record -F 99 -t <mainpid> -g -- sleep 30
perf report --no-children --stdio | head -60
```

Compare inclusive shares of RouteLooksOpen, BuildPolyPath, FillNearestVendor, HasFullRouteTo against the table above. Also watch the module's `pump:` log lines and the world update interval (target well under 100 ms at 495 bots). A bot that is shown no vendor for a long time after a restart is the cold cache plus the 6-search budget filling up; it clears within a few minutes.

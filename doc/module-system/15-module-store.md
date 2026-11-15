# 15. Module store

Each module can keep small key/value rows in the world database, private to that module. Modules never send SQL.

## Who can reach what

- Table `module_store` (world database; `sql/updates/world/master/2026_10_12_00_world.sql`): columns `module`, `key`, `value`.
- A module's requests carry no module name. The core takes the module from the session, so a module reaches only its own
  rows.
- The protocol uses the action path (`StoreGet`, `StoreSet`, `StoreDelete`, `StoreList`, actions 12 to 15 in
  `actions.proto`). The reply sets `found`, `value` and `keys`.

## Limits

| Limit | Value |
|-------|-------|
| Key | 1 to 128 printable ASCII characters, no spaces |
| Value | at most 1024 bytes |
| Rows per module | `Modules.Store.MaxRowsPerModule`, default 5000, clamped to 100 .. 20000 (replacing an existing key does not count) |
| Keys per list | the same limit, sorted, and at most about 900 KB of keys so the reply fits one wire frame |

Over the limit a new row is refused and a list is cut off; neither is silent: the core logs one warning per module
(logger `modules`) naming `Modules.Store.MaxRowsPerModule`. Lowering the limit below the rows already stored keeps them
but refuses new rows. The old fixed limit was 1000, which left bot characters above 1000 unsaved.

A refused request gets `success = false` and a `reason`, which the module can log.

## Behaviour

- Runs on the world thread and waits for the database. It is a few small queries per request, so the wait is short;
  `StoreList` on a module with many rows is the largest.
- A write is read back before the reply is sent, because the database layer logs failed writes but does not return them.
  A write that did not land is reported as a failure.
- A module that has been unloaded gets "module is no longer loaded" for any store request.

## Applying the schema

A server that runs the database updates picks up `2026_10_12_00_world.sql` at start-up. Otherwise run that file against
the world database once, before starting the worldserver.

## Used by

- `forever-bots` keeps one row per controlled bot, `bot/<guid>`, and reclaims those bots on start (replacing the local state
  file).

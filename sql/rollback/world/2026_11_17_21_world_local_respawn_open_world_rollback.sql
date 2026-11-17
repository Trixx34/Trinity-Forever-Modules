-- Rollback for sql/custom/world/2026_11_17_21_world_local_respawn_open_world.sql. NOT run by the updater: run it by hand on the world database (forever_*_world); restart the worldserver afterwards.
-- Exact undo without a before-image: the file only INSERTs rows with group id 901036 (verified free on the sim 2026-11-17: no template or member with that id existed, no group id >= 900000 in use).
-- No other group and no creature row is touched; the spawns simply lose the group (they are in no other group, the file skipped grouped spawns).
DELETE FROM `spawn_group` WHERE `groupId` = 901036;
DELETE FROM `spawn_group_template` WHERE `groupId` = 901036;
-- Afterwards `DELETE FROM updates WHERE name = '2026_11_17_21_world_local_respawn_open_world.sql';` if the file is to run again.

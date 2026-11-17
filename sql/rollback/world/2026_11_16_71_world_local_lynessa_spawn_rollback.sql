-- Rollback for sql/custom/world/2026_11_16_71_world_local_lynessa_spawn.sql. NOT run by the updater: run it by hand on the world database (forever_*_world); restart the worldserver afterwards.
-- Generated from the file's own statements: the active DELETEs remove the rows the file wrote (same keys the file deletes before it inserts).
-- Rows the file itself DELETED or UPDATED cannot be restored exactly without a before-image. Take it BEFORE the file is applied (STEP 0), then use STEP 2.
--
-- STEP 0 (before applying the file, optional but needed for an exact undo of deleted/updated rows):
--   CREATE TABLE `bak_75bd66cf_creature` AS SELECT * FROM `creature` WHERE (`guid` IN (27574400,27574401,27574402) AND `id` = 275744) OR (guid = 27574402 AND id = 275744 AND map = 1 AND (ABS(position_x - 9869.66015625) < 0.001 AND ABS(position_y - 1558.9699707) < 0.001 AND ABS(position_z - 1328.55004883) < 0.001 AND ABS(orientation - 0) < 0.001));
--   CREATE TABLE `bak_75bd66cf_pool_members` AS SELECT * FROM `pool_members` WHERE (`type` = 0 AND `spawnId` IN (27574400,27574401,27574402));
--   CREATE TABLE `bak_75bd66cf_pool_template` AS SELECT * FROM `pool_template` WHERE (`entry` = 1302288);
--
-- STEP 1 (the undo that needs no before-image):
DELETE FROM `creature` WHERE (`guid` IN (27574400,27574401,27574402) AND `id` = 275744);   -- the file's own delete condition: removes what it wrote
DELETE FROM `pool_members` WHERE (`type` = 0 AND `spawnId` IN (27574400,27574401,27574402));   -- the file's own delete condition: removes what it wrote
DELETE FROM `pool_template` WHERE (`entry` = 1302288);   -- the file's own delete condition: removes what it wrote
--
-- STEP 2 (only if STEP 0 was done before applying: put the old rows back, then drop the backup):
--   REPLACE INTO `creature` SELECT * FROM `bak_75bd66cf_creature`;  DROP TABLE `bak_75bd66cf_creature`;
--   REPLACE INTO `pool_members` SELECT * FROM `bak_75bd66cf_pool_members`;  DROP TABLE `bak_75bd66cf_pool_members`;
--   REPLACE INTO `pool_template` SELECT * FROM `bak_75bd66cf_pool_template`;  DROP TABLE `bak_75bd66cf_pool_template`;
-- Afterwards `DELETE FROM updates WHERE name = '2026_11_16_71_world_local_lynessa_spawn.sql';` if the file is to run again.

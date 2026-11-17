-- Rollback for sql/custom/world/2026_11_14_12_world_forever_quest_giver_spawn_duplicates.sql. NOT run by the updater: run it by hand on the world database (forever_*_world); restart the worldserver afterwards.
-- Generated from the file's own statements: the active DELETEs remove the rows the file wrote (same keys the file deletes before it inserts).
-- Rows the file itself DELETED or UPDATED cannot be restored exactly without a before-image. Take it BEFORE the file is applied (STEP 0), then use STEP 2.
--
-- STEP 0 (before applying the file, optional but needed for an exact undo of deleted/updated rows):
--   CREATE TABLE `bak_ea745096_creature` AS SELECT * FROM `creature` WHERE (`guid` IN (SELECT `guid` FROM `tmp_dup_46`));
--   CREATE TABLE `bak_ea745096_creature_addon` AS SELECT * FROM `creature_addon` WHERE (`guid` = 1424200 AND NOT EXISTS (SELECT 1 FROM `creature` WHERE `guid` = 1424200)) OR (`guid` IN (SELECT `guid` FROM `tmp_dup_46`));
--   CREATE TABLE `bak_ea745096_pool_members` AS SELECT * FROM `pool_members` WHERE (`type` = 0 AND `spawnId` IN (SELECT `guid` FROM `tmp_dup_46`));
--   CREATE TABLE `bak_ea745096_spawn_group` AS SELECT * FROM `spawn_group` WHERE (`spawnType` = 0 AND `spawnId` IN (SELECT `guid` FROM `tmp_dup_46`));
--
-- STEP 1 (the undo that needs no before-image):
DELETE FROM `creature` WHERE (`guid` IN (SELECT `guid` FROM `tmp_dup_46`));   -- the file's own delete condition: removes what it wrote
DELETE FROM `creature_addon` WHERE (`guid` = 1424200 AND NOT EXISTS (SELECT 1 FROM `creature` WHERE `guid` = 1424200)) OR (`guid` IN (SELECT `guid` FROM `tmp_dup_46`));   -- the file's own delete condition: removes what it wrote
DELETE FROM `pool_members` WHERE (`type` = 0 AND `spawnId` IN (SELECT `guid` FROM `tmp_dup_46`));   -- the file's own delete condition: removes what it wrote
DELETE FROM `spawn_group` WHERE (`spawnType` = 0 AND `spawnId` IN (SELECT `guid` FROM `tmp_dup_46`));   -- the file's own delete condition: removes what it wrote
--
-- STEP 2 (only if STEP 0 was done before applying: put the old rows back, then drop the backup):
--   REPLACE INTO `creature` SELECT * FROM `bak_ea745096_creature`;  DROP TABLE `bak_ea745096_creature`;
--   REPLACE INTO `creature_addon` SELECT * FROM `bak_ea745096_creature_addon`;  DROP TABLE `bak_ea745096_creature_addon`;
--   REPLACE INTO `pool_members` SELECT * FROM `bak_ea745096_pool_members`;  DROP TABLE `bak_ea745096_pool_members`;
--   REPLACE INTO `spawn_group` SELECT * FROM `bak_ea745096_spawn_group`;  DROP TABLE `bak_ea745096_spawn_group`;
-- Afterwards `DELETE FROM updates WHERE name = '2026_11_14_12_world_forever_quest_giver_spawn_duplicates.sql';` if the file is to run again.

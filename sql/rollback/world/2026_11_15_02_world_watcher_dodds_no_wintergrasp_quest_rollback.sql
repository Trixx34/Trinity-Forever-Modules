-- Rollback for sql/custom/world/2026_11_15_02_world_watcher_dodds_no_wintergrasp_quest.sql. NOT run by the updater: run it by hand on the world database (forever_*_world); restart the worldserver afterwards.
-- Generated from the file's own statements: the active DELETEs remove the rows the file wrote (same keys the file deletes before it inserts).
-- Rows the file itself DELETED or UPDATED cannot be restored exactly without a before-image. Take it BEFORE the file is applied (STEP 0), then use STEP 2.
--
-- STEP 0 (before applying the file, optional but needed for an exact undo of deleted/updated rows):
--   CREATE TABLE `bak_f0e3cced_creature_questender` AS SELECT * FROM `creature_questender` WHERE (`id` = 888 AND `quest` = 236);
--   CREATE TABLE `bak_f0e3cced_creature_queststarter` AS SELECT * FROM `creature_queststarter` WHERE (`id` = 888 AND `quest` = 236);
--
-- STEP 1 (the undo that needs no before-image):
DELETE FROM `creature_questender` WHERE (`id` = 888 AND `quest` = 236);   -- the file's own delete condition: removes what it wrote
DELETE FROM `creature_queststarter` WHERE (`id` = 888 AND `quest` = 236);   -- the file's own delete condition: removes what it wrote
--
-- STEP 2 (only if STEP 0 was done before applying: put the old rows back, then drop the backup):
--   REPLACE INTO `creature_questender` SELECT * FROM `bak_f0e3cced_creature_questender`;  DROP TABLE `bak_f0e3cced_creature_questender`;
--   REPLACE INTO `creature_queststarter` SELECT * FROM `bak_f0e3cced_creature_queststarter`;  DROP TABLE `bak_f0e3cced_creature_queststarter`;
-- Afterwards `DELETE FROM updates WHERE name = '2026_11_15_02_world_watcher_dodds_no_wintergrasp_quest.sql';` if the file is to run again.

-- Rollback for sql/custom/world/2026_11_15_56_world_forever_live_sniff_ironforge_1115.sql. NOT run by the updater: run it by hand on the world database (forever_*_world); restart the worldserver afterwards.
-- Generated from the file's own statements: the active DELETEs remove the rows the file wrote (same keys the file deletes before it inserts).
-- Rows the file itself DELETED or UPDATED cannot be restored exactly without a before-image. Take it BEFORE the file is applied (STEP 0), then use STEP 2.
--
-- STEP 0 (before applying the file, optional but needed for an exact undo of deleted/updated rows):
--   CREATE TABLE `bak_d86d31a4_creature` AS SELECT * FROM `creature` WHERE (`guid` BETWEEN 46110000 AND 46110004) OR (`guid` = 9687046 AND `id` = 267684) OR (`guid` = 9953285 AND `id` = 276735) OR (`guid` = 20000269 AND `id` = 10610) OR (`guid` = 20000267 AND `id` = 10611) OR (`guid` = 20000266 AND `id` = 10803) OR (`guid` = 20000268 AND `id` = 10804) OR (`guid` = 20000272 AND `id` = 10805);
--   CREATE TABLE `bak_d86d31a4_gameobject` AS SELECT * FROM `gameobject` WHERE (`guid` = 30001821 AND `id` = 171649) OR (`guid` = 838001001 AND `id` = 630865) OR (`guid` BETWEEN 46110000 AND 46110014);
--
-- STEP 1 (the undo that needs no before-image):
DELETE FROM `creature` WHERE (`guid` BETWEEN 46110000 AND 46110004);   -- the file's own delete condition: removes what it wrote
DELETE FROM `gameobject` WHERE (`guid` = 30001821 AND `id` = 171649) OR (`guid` = 838001001 AND `id` = 630865) OR (`guid` BETWEEN 46110000 AND 46110014);   -- the file's own delete condition: removes what it wrote
DELETE FROM `gameobject_template` WHERE `entry` IN (630860,650705,650706,650707,650708,650710,650711,650712,650713,650714,650716,650718,650719,707346);
--
-- STEP 2 (only if STEP 0 was done before applying: put the old rows back, then drop the backup):
--   REPLACE INTO `creature` SELECT * FROM `bak_d86d31a4_creature`;  DROP TABLE `bak_d86d31a4_creature`;
--   REPLACE INTO `gameobject` SELECT * FROM `bak_d86d31a4_gameobject`;  DROP TABLE `bak_d86d31a4_gameobject`;
-- Afterwards `DELETE FROM updates WHERE name = '2026_11_15_56_world_forever_live_sniff_ironforge_1115.sql';` if the file is to run again.

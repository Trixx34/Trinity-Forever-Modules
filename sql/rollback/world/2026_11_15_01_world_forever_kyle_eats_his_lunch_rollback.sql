-- Rollback for sql/custom/world/2026_11_15_01_world_forever_kyle_eats_his_lunch.sql. NOT run by the updater: run it by hand on the world database (forever_*_world); restart the worldserver afterwards.
-- Generated from the file's own statements: the active DELETEs remove the rows the file wrote (same keys the file deletes before it inserts).
-- Rows the file itself DELETED or UPDATED cannot be restored exactly without a before-image. Take it BEFORE the file is applied (STEP 0), then use STEP 2.
--
-- STEP 0 (before applying the file, optional but needed for an exact undo of deleted/updated rows):
--   CREATE TABLE `bak_6dcd3c65_conditions` AS SELECT * FROM `conditions` WHERE (`SourceTypeOrReferenceId` = 13 AND `SourceEntry` = 1323075);
--   CREATE TABLE `bak_6dcd3c65_smart_scripts` AS SELECT * FROM `smart_scripts` WHERE (`entryorguid` = 27715400 AND `source_type` = 9);
--
-- STEP 1 (the undo that needs no before-image):
DELETE FROM `conditions` WHERE (`SourceTypeOrReferenceId` = 13 AND `SourceEntry` = 1323075);   -- the file's own delete condition: removes what it wrote
DELETE FROM `smart_scripts` WHERE (`entryorguid` = 27715400 AND `source_type` = 9);   -- the file's own delete condition: removes what it wrote
--
-- STEP 2 (only if STEP 0 was done before applying: put the old rows back, then drop the backup):
--   REPLACE INTO `conditions` SELECT * FROM `bak_6dcd3c65_conditions`;  DROP TABLE `bak_6dcd3c65_conditions`;
--   REPLACE INTO `smart_scripts` SELECT * FROM `bak_6dcd3c65_smart_scripts`;  DROP TABLE `bak_6dcd3c65_smart_scripts`;
-- Afterwards `DELETE FROM updates WHERE name = '2026_11_15_01_world_forever_kyle_eats_his_lunch.sql';` if the file is to run again.

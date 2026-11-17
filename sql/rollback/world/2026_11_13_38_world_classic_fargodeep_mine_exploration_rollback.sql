-- Rollback for sql/custom/world/2026_11_13_38_world_classic_fargodeep_mine_exploration.sql. NOT run by the updater: run it by hand on the world database (forever_*_world); restart the worldserver afterwards.
-- Generated from the file's own statements: the active DELETEs remove the rows the file wrote (same keys the file deletes before it inserts).
-- Rows the file itself DELETED or UPDATED cannot be restored exactly without a before-image. Take it BEFORE the file is applied (STEP 0), then use STEP 2.
--
-- STEP 0 (before applying the file, optional but needed for an exact undo of deleted/updated rows):
--   CREATE TABLE `bak_535fdca9_areatrigger` AS SELECT * FROM `areatrigger` WHERE (`SpawnId` IN (9206, 9207));
--   CREATE TABLE `bak_535fdca9_areatrigger_create_properties` AS SELECT * FROM `areatrigger_create_properties` WHERE (`Id` IN (620, 621) AND `IsCustom` = 1);
--   CREATE TABLE `bak_535fdca9_areatrigger_template` AS SELECT * FROM `areatrigger_template` WHERE (`Id` = 620 AND `IsCustom` = 1);
--   CREATE TABLE `bak_535fdca9_smart_scripts` AS SELECT * FROM `smart_scripts` WHERE (`entryorguid` = 620 AND `source_type` = 12);
--
-- STEP 1 (the undo that needs no before-image):
DELETE FROM `areatrigger` WHERE (`SpawnId` IN (9206, 9207));   -- the file's own delete condition: removes what it wrote
DELETE FROM `areatrigger_create_properties` WHERE (`Id` IN (620, 621) AND `IsCustom` = 1);   -- the file's own delete condition: removes what it wrote
DELETE FROM `areatrigger_template` WHERE (`Id` = 620 AND `IsCustom` = 1);   -- the file's own delete condition: removes what it wrote
DELETE FROM `smart_scripts` WHERE (`entryorguid` = 620 AND `source_type` = 12);   -- the file's own delete condition: removes what it wrote
--
-- STEP 2 (only if STEP 0 was done before applying: put the old rows back, then drop the backup):
--   REPLACE INTO `areatrigger` SELECT * FROM `bak_535fdca9_areatrigger`;  DROP TABLE `bak_535fdca9_areatrigger`;
--   REPLACE INTO `areatrigger_create_properties` SELECT * FROM `bak_535fdca9_areatrigger_create_properties`;  DROP TABLE `bak_535fdca9_areatrigger_create_properties`;
--   REPLACE INTO `areatrigger_template` SELECT * FROM `bak_535fdca9_areatrigger_template`;  DROP TABLE `bak_535fdca9_areatrigger_template`;
--   REPLACE INTO `smart_scripts` SELECT * FROM `bak_535fdca9_smart_scripts`;  DROP TABLE `bak_535fdca9_smart_scripts`;
-- Afterwards `DELETE FROM updates WHERE name = '2026_11_13_38_world_classic_fargodeep_mine_exploration.sql';` if the file is to run again.

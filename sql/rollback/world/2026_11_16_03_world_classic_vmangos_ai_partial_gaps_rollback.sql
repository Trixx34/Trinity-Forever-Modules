-- Rollback for sql/custom/world/2026_11_16_03_world_classic_vmangos_ai_partial_gaps.sql. NOT run by the updater: run it by hand on the world database (forever_*_world); restart the worldserver afterwards.
-- Generated from the file's own statements: the active DELETEs remove the rows the file wrote (same keys the file deletes before it inserts).
-- Rows the file itself DELETED or UPDATED cannot be restored exactly without a before-image. Take it BEFORE the file is applied (STEP 0), then use STEP 2.
--
-- STEP 0 (before applying the file, optional but needed for an exact undo of deleted/updated rows):
--   CREATE TABLE `bak_ffd6f202_conditions` AS SELECT * FROM `conditions` WHERE (`SourceTypeOrReferenceId` = 22 AND `SourceGroup` = 2 AND `SourceEntry` = 7463 AND `SourceId` = 0);
--   CREATE TABLE `bak_ffd6f202_creature_text` AS SELECT * FROM `creature_text` WHERE ((`CreatureID` = 7157 AND `GroupID` = 0 AND `ID` = 0) OR (`CreatureID` = 11880 AND `GroupID` = 1 AND `ID` = 0));
--   CREATE TABLE `bak_ffd6f202_smart_scripts` AS SELECT * FROM `smart_scripts` WHERE (`source_type` = 0 AND ((`entryorguid` = 7156 AND `id` = 2) OR (`entryorguid` = 7157 AND `id` IN (1, 2, 3)) OR (`entryorguid` = 7158 AND `id` = 4) OR (`entryorguid` = 7463 AND `id` = 1) OR (`entryorguid` = 11880 AND `id` IN (3, 4, 5)) OR (`entryorguid` = 11881 AND `id` = 3) OR (`entryorguid` = 15213 AND `id` = 6)));
--
-- STEP 1 (the undo that needs no before-image):
DELETE FROM `conditions` WHERE (`SourceTypeOrReferenceId` = 22 AND `SourceGroup` = 2 AND `SourceEntry` = 7463 AND `SourceId` = 0);   -- the file's own delete condition: removes what it wrote
DELETE FROM `creature_text` WHERE ((`CreatureID` = 7157 AND `GroupID` = 0 AND `ID` = 0) OR (`CreatureID` = 11880 AND `GroupID` = 1 AND `ID` = 0));   -- the file's own delete condition: removes what it wrote
DELETE FROM `smart_scripts` WHERE (`source_type` = 0 AND ((`entryorguid` = 7156 AND `id` = 2) OR (`entryorguid` = 7157 AND `id` IN (1, 2, 3)) OR (`entryorguid` = 7158 AND `id` = 4) OR (`entryorguid` = 7463 AND `id` = 1) OR (`entryorguid` = 11880 AND `id` IN (3, 4, 5)) OR (`entryorguid` = 11881 AND `id` = 3) OR (`entryorguid` = 15213 AND `id` = 6)));   -- the file's own delete condition: removes what it wrote
--
-- STEP 2 (only if STEP 0 was done before applying: put the old rows back, then drop the backup):
--   REPLACE INTO `conditions` SELECT * FROM `bak_ffd6f202_conditions`;  DROP TABLE `bak_ffd6f202_conditions`;
--   REPLACE INTO `creature_text` SELECT * FROM `bak_ffd6f202_creature_text`;  DROP TABLE `bak_ffd6f202_creature_text`;
--   REPLACE INTO `smart_scripts` SELECT * FROM `bak_ffd6f202_smart_scripts`;  DROP TABLE `bak_ffd6f202_smart_scripts`;
-- Afterwards `DELETE FROM updates WHERE name = '2026_11_16_03_world_classic_vmangos_ai_partial_gaps.sql';` if the file is to run again.

-- Rollback for sql/custom/world/2026_11_16_02_world_classic_vmangos_ai_not_converted.sql. NOT run by the updater: run it by hand on the world database (forever_*_world); restart the worldserver afterwards.
-- Generated from the file's own statements: the active DELETEs remove the rows the file wrote (same keys the file deletes before it inserts).
-- Rows the file itself DELETED or UPDATED cannot be restored exactly without a before-image. Take it BEFORE the file is applied (STEP 0), then use STEP 2.
--
-- STEP 0 (before applying the file, optional but needed for an exact undo of deleted/updated rows):
--   CREATE TABLE `bak_6ee22d88_creature_template` AS SELECT * FROM `creature_template` WHERE (`entry` IN (1946, 1981, 2434, 4196, 4264, 7805, 11296, 11636, 11637, 11875, 12385, 12904, 14081, 14101, 15917) AND `ScriptName` = '' AND `AIName` IN ('', 'SmartAI'));
--   CREATE TABLE `bak_6ee22d88_creature_text` AS SELECT * FROM `creature_text` WHERE (`CreatureID` IN (1755, 1946, 4264, 11296, 11636, 11637) AND `comment` = 'VMaNGOS EventAI');
--   CREATE TABLE `bak_6ee22d88_smart_scripts` AS SELECT * FROM `smart_scripts` WHERE (`source_type` = 0 AND `comment` LIKE 'VMaNGOS EventAI %' AND `entryorguid` IN (1755, 1946, 1981, 2434, 4196, 4264, 7805, 11296, 11636, 11637, 11875, 12385, 12904, 14081, 14101, 15917));
--
-- STEP 1 (the undo that needs no before-image):
-- creature_template: only UPDATE in the file: nothing to remove; old values come back only through STEP 2.
DELETE FROM `creature_text` WHERE (`CreatureID` IN (1755, 1946, 4264, 11296, 11636, 11637) AND `comment` = 'VMaNGOS EventAI');   -- the file's own delete condition: removes what it wrote
DELETE FROM `smart_scripts` WHERE (`source_type` = 0 AND `comment` LIKE 'VMaNGOS EventAI %' AND `entryorguid` IN (1755, 1946, 1981, 2434, 4196, 4264, 7805, 11296, 11636, 11637, 11875, 12385, 12904, 14081, 14101, 15917));   -- the file's own delete condition: removes what it wrote
--
-- STEP 2 (only if STEP 0 was done before applying: put the old rows back, then drop the backup):
--   REPLACE INTO `creature_template` SELECT * FROM `bak_6ee22d88_creature_template`;  DROP TABLE `bak_6ee22d88_creature_template`;
--   REPLACE INTO `creature_text` SELECT * FROM `bak_6ee22d88_creature_text`;  DROP TABLE `bak_6ee22d88_creature_text`;
--   REPLACE INTO `smart_scripts` SELECT * FROM `bak_6ee22d88_smart_scripts`;  DROP TABLE `bak_6ee22d88_smart_scripts`;
-- Afterwards `DELETE FROM updates WHERE name = '2026_11_16_02_world_classic_vmangos_ai_not_converted.sql';` if the file is to run again.

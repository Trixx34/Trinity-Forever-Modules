-- Rollback for sql/custom/world/2026_11_16_05_world_classic_knot_thimblejack_talk_flag.sql. NOT run by the updater: run it by hand on the world database (forever_*_world); restart the worldserver afterwards.
-- Generated from the file's own statements: the active DELETEs remove the rows the file wrote (same keys the file deletes before it inserts).
-- Rows the file itself DELETED or UPDATED cannot be restored exactly without a before-image. Take it BEFORE the file is applied (STEP 0), then use STEP 2.
--
-- STEP 0 (before applying the file, optional but needed for an exact undo of deleted/updated rows):
--   CREATE TABLE `bak_463fb7e7_creature_template` AS SELECT * FROM `creature_template` WHERE (`entry` = 14338);
--   CREATE TABLE `bak_463fb7e7_creature_template_gossip` AS SELECT * FROM `creature_template_gossip` WHERE (`CreatureID` = 14338);
--   CREATE TABLE `bak_463fb7e7_gossip_menu` AS SELECT * FROM `gossip_menu` WHERE (`MenuID` = 91014338);
--
-- STEP 1 (the undo that needs no before-image):
-- creature_template: only UPDATE in the file: nothing to remove; old values come back only through STEP 2.
DELETE FROM `creature_template_gossip` WHERE (`CreatureID` = 14338);   -- the file's own delete condition: removes what it wrote
DELETE FROM `gossip_menu` WHERE (`MenuID` = 91014338);   -- the file's own delete condition: removes what it wrote
--
-- STEP 2 (only if STEP 0 was done before applying: put the old rows back, then drop the backup):
--   REPLACE INTO `creature_template` SELECT * FROM `bak_463fb7e7_creature_template`;  DROP TABLE `bak_463fb7e7_creature_template`;
--   REPLACE INTO `creature_template_gossip` SELECT * FROM `bak_463fb7e7_creature_template_gossip`;  DROP TABLE `bak_463fb7e7_creature_template_gossip`;
--   REPLACE INTO `gossip_menu` SELECT * FROM `bak_463fb7e7_gossip_menu`;  DROP TABLE `bak_463fb7e7_gossip_menu`;
-- Afterwards `DELETE FROM updates WHERE name = '2026_11_16_05_world_classic_knot_thimblejack_talk_flag.sql';` if the file is to run again.

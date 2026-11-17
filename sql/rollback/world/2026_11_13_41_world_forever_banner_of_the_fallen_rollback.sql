-- Rollback for sql/custom/world/2026_11_13_41_world_forever_banner_of_the_fallen.sql. NOT run by the updater: run it by hand on the world database (forever_*_world); restart the worldserver afterwards.
-- Generated from the file's own statements: the active DELETEs remove the rows the file wrote (same keys the file deletes before it inserts).
-- Rows the file itself DELETED or UPDATED cannot be restored exactly without a before-image. Take it BEFORE the file is applied (STEP 0), then use STEP 2.
--
-- STEP 0 (before applying the file, optional but needed for an exact undo of deleted/updated rows):
--   CREATE TABLE `bak_73b398e6_creature_questender` AS SELECT * FROM `creature_questender` WHERE (`quest` = 86585);
--   CREATE TABLE `bak_73b398e6_creature_queststarter` AS SELECT * FROM `creature_queststarter` WHERE (`quest` = 86585);
--   CREATE TABLE `bak_73b398e6_quest_classic_level` AS SELECT * FROM `quest_classic_level` WHERE (`ID` = 86585);
--   CREATE TABLE `bak_73b398e6_quest_objectives` AS SELECT * FROM `quest_objectives` WHERE (`QuestID` = 86585);
--   CREATE TABLE `bak_73b398e6_quest_poi` AS SELECT * FROM `quest_poi` WHERE (`QuestID` = 86585);
--   CREATE TABLE `bak_73b398e6_quest_poi_points` AS SELECT * FROM `quest_poi_points` WHERE (`QuestID` = 86585);
--   CREATE TABLE `bak_73b398e6_quest_template` AS SELECT * FROM `quest_template` WHERE (`ID` = 86585);
--   CREATE TABLE `bak_73b398e6_quest_template_addon` AS SELECT * FROM `quest_template_addon` WHERE (`ID` = 86585);
--   CREATE TABLE `bak_73b398e6_quest_template_classic_level` AS SELECT * FROM `quest_template_classic_level` WHERE (`ID` = 86585);
--
-- STEP 1 (the undo that needs no before-image):
DELETE FROM `creature_questender` WHERE (`quest` = 86585);   -- the file's own delete condition: removes what it wrote
DELETE FROM `creature_queststarter` WHERE (`quest` = 86585);   -- the file's own delete condition: removes what it wrote
DELETE FROM `quest_classic_level` WHERE (`ID` = 86585);   -- the file's own delete condition: removes what it wrote
DELETE FROM `quest_objectives` WHERE (`QuestID` = 86585);   -- the file's own delete condition: removes what it wrote
DELETE FROM `quest_poi` WHERE (`QuestID` = 86585);   -- the file's own delete condition: removes what it wrote
DELETE FROM `quest_poi_points` WHERE (`QuestID` = 86585);   -- the file's own delete condition: removes what it wrote
DELETE FROM `quest_template` WHERE (`ID` = 86585);   -- the file's own delete condition: removes what it wrote
DELETE FROM `quest_template_addon` WHERE (`ID` = 86585);   -- the file's own delete condition: removes what it wrote
DELETE FROM `quest_template_classic_level` WHERE (`ID` = 86585);   -- the file's own delete condition: removes what it wrote
--
-- STEP 2 (only if STEP 0 was done before applying: put the old rows back, then drop the backup):
--   REPLACE INTO `creature_questender` SELECT * FROM `bak_73b398e6_creature_questender`;  DROP TABLE `bak_73b398e6_creature_questender`;
--   REPLACE INTO `creature_queststarter` SELECT * FROM `bak_73b398e6_creature_queststarter`;  DROP TABLE `bak_73b398e6_creature_queststarter`;
--   REPLACE INTO `quest_classic_level` SELECT * FROM `bak_73b398e6_quest_classic_level`;  DROP TABLE `bak_73b398e6_quest_classic_level`;
--   REPLACE INTO `quest_objectives` SELECT * FROM `bak_73b398e6_quest_objectives`;  DROP TABLE `bak_73b398e6_quest_objectives`;
--   REPLACE INTO `quest_poi` SELECT * FROM `bak_73b398e6_quest_poi`;  DROP TABLE `bak_73b398e6_quest_poi`;
--   REPLACE INTO `quest_poi_points` SELECT * FROM `bak_73b398e6_quest_poi_points`;  DROP TABLE `bak_73b398e6_quest_poi_points`;
--   REPLACE INTO `quest_template` SELECT * FROM `bak_73b398e6_quest_template`;  DROP TABLE `bak_73b398e6_quest_template`;
--   REPLACE INTO `quest_template_addon` SELECT * FROM `bak_73b398e6_quest_template_addon`;  DROP TABLE `bak_73b398e6_quest_template_addon`;
--   REPLACE INTO `quest_template_classic_level` SELECT * FROM `bak_73b398e6_quest_template_classic_level`;  DROP TABLE `bak_73b398e6_quest_template_classic_level`;
-- Afterwards `DELETE FROM updates WHERE name = '2026_11_13_41_world_forever_banner_of_the_fallen.sql';` if the file is to run again.

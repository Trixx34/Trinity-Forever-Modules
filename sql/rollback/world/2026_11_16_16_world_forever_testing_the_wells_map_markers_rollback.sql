-- Rollback for sql/custom/world/2026_11_16_16_world_forever_testing_the_wells_map_markers.sql. NOT run by the updater: run it by hand on the world database (forever_*_world); restart the worldserver afterwards.
-- Generated from the file's own statements: the active DELETEs remove the rows the file wrote (same keys the file deletes before it inserts).
-- Rows the file itself DELETED or UPDATED cannot be restored exactly without a before-image. Take it BEFORE the file is applied (STEP 0), then use STEP 2.
--
-- STEP 0 (before applying the file, optional but needed for an exact undo of deleted/updated rows):
--   CREATE TABLE `bak_f071c725_quest_poi` AS SELECT * FROM `quest_poi` WHERE (`QuestID` IN (92742,92744));
--   CREATE TABLE `bak_f071c725_quest_poi_points` AS SELECT * FROM `quest_poi_points` WHERE (`QuestID` IN (92742,92744));
--
-- STEP 1 (the undo that needs no before-image):
DELETE FROM `quest_poi` WHERE (`QuestID` IN (92742,92744));   -- the file's own delete condition: removes what it wrote
DELETE FROM `quest_poi_points` WHERE (`QuestID` IN (92742,92744));   -- the file's own delete condition: removes what it wrote
--
-- STEP 2 (only if STEP 0 was done before applying: put the old rows back, then drop the backup):
--   REPLACE INTO `quest_poi` SELECT * FROM `bak_f071c725_quest_poi`;  DROP TABLE `bak_f071c725_quest_poi`;
--   REPLACE INTO `quest_poi_points` SELECT * FROM `bak_f071c725_quest_poi_points`;  DROP TABLE `bak_f071c725_quest_poi_points`;
-- Afterwards `DELETE FROM updates WHERE name = '2026_11_16_16_world_forever_testing_the_wells_map_markers.sql';` if the file is to run again.

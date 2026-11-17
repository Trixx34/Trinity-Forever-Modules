-- Rollback for sql/custom/world/2026_11_14_01_world_forever_lesson_in_divinity_venomweb_plans.sql. NOT run by the updater: run it by hand on the world database (forever_*_world); restart the worldserver afterwards.
-- Generated from the file's own statements: the active DELETEs remove the rows the file wrote (same keys the file deletes before it inserts).
-- Rows the file itself DELETED or UPDATED cannot be restored exactly without a before-image. Take it BEFORE the file is applied (STEP 0), then use STEP 2.
--
-- STEP 0 (before applying the file, optional but needed for an exact undo of deleted/updated rows):
--   CREATE TABLE `bak_b83cd1db_quest_poi` AS SELECT * FROM `quest_poi` WHERE (`QuestID` = 94440 AND `BlobIndex` = 1 AND `Idx1` = 2);
--   CREATE TABLE `bak_b83cd1db_quest_poi_points` AS SELECT * FROM `quest_poi_points` WHERE (`QuestID` = 94440 AND `Idx1` = 2);
--
-- STEP 1 (the undo that needs no before-image):
-- creature_loot_template: the file inserts rows that are not listed with their keys (INSERT ... SELECT / ON DUPLICATE KEY). Exact undo needs the before-image:
--   STEP 0: CREATE TABLE `bak_b83cd1db_creature_loot_template` AS SELECT * FROM `creature_loot_template`;   (full copy, before the file is applied)
--   STEP 1b: DELETE x FROM `creature_loot_template` x LEFT JOIN `bak_b83cd1db_creature_loot_template` b ON x.`Entry` = b.`Entry` AND x.`ItemType` = b.`ItemType` AND x.`Item` = b.`Item` WHERE b.`Entry` IS NULL;   (rows the file added)
--   STEP 2: REPLACE INTO `creature_loot_template` SELECT * FROM `bak_b83cd1db_creature_loot_template`;  DROP TABLE `bak_b83cd1db_creature_loot_template`;   (changed rows back)
DELETE FROM `quest_poi` WHERE (`QuestID` = 94440 AND `BlobIndex` = 1 AND `Idx1` = 2);   -- the file's own delete condition: removes what it wrote
DELETE FROM `quest_poi_points` WHERE (`QuestID` = 94440 AND `Idx1` = 2);   -- the file's own delete condition: removes what it wrote
--
-- STEP 2 (only if STEP 0 was done before applying: put the old rows back, then drop the backup):
--   REPLACE INTO `quest_poi` SELECT * FROM `bak_b83cd1db_quest_poi`;  DROP TABLE `bak_b83cd1db_quest_poi`;
--   REPLACE INTO `quest_poi_points` SELECT * FROM `bak_b83cd1db_quest_poi_points`;  DROP TABLE `bak_b83cd1db_quest_poi_points`;
-- Afterwards `DELETE FROM updates WHERE name = '2026_11_14_01_world_forever_lesson_in_divinity_venomweb_plans.sql';` if the file is to run again.

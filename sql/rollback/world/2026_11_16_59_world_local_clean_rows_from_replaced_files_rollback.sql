-- Rollback for sql/custom/world/2026_11_16_59_world_local_clean_rows_from_replaced_files.sql. NOT run by the updater: run it by hand on the world database (forever_*_world); restart the worldserver afterwards.
-- Generated from the file's own statements: the active DELETEs remove the rows the file wrote (same keys the file deletes before it inserts).
-- Rows the file itself DELETED or UPDATED cannot be restored exactly without a before-image. Take it BEFORE the file is applied (STEP 0), then use STEP 2.
--
-- STEP 0 (before applying the file, optional but needed for an exact undo of deleted/updated rows):
--   CREATE TABLE `bak_d3c1c14b_gameobject` AS SELECT * FROM `gameobject` WHERE (guid=838000800 AND id=8380008);
--   CREATE TABLE `bak_d3c1c14b_gameobject_template` AS SELECT * FROM `gameobject_template` WHERE (entry=8380008);
--   CREATE TABLE `bak_d3c1c14b_pool_members` AS SELECT * FROM `pool_members` WHERE (type=0 AND poolSpawnId=2706930);
--   CREATE TABLE `bak_d3c1c14b_pool_template` AS SELECT * FROM `pool_template` WHERE (entry=2706930);
--   CREATE TABLE `bak_d3c1c14b_quest_poi` AS SELECT * FROM `quest_poi` WHERE (QuestID=86667 AND Idx1=1);
--   CREATE TABLE `bak_d3c1c14b_quest_poi_points` AS SELECT * FROM `quest_poi_points` WHERE (QuestID=95999 AND Idx1=1 AND Idx2=0 AND X=-9721 AND Y=-3189) OR (QuestID=86667 AND Idx1=1);
--
-- STEP 1 (the undo that needs no before-image):
DELETE FROM `gameobject` WHERE (guid=838000800 AND id=8380008);   -- the file's own delete condition: removes what it wrote
DELETE FROM `gameobject_template` WHERE (entry=8380008);   -- the file's own delete condition: removes what it wrote
DELETE FROM `pool_members` WHERE (type=0 AND poolSpawnId=2706930);   -- the file's own delete condition: removes what it wrote
DELETE FROM `pool_template` WHERE (entry=2706930);   -- the file's own delete condition: removes what it wrote
DELETE FROM `quest_poi` WHERE (QuestID=86667 AND Idx1=1);   -- the file's own delete condition: removes what it wrote
DELETE FROM `quest_poi_points` WHERE (QuestID=86667 AND Idx1=1);   -- the file's own delete condition: removes what it wrote
--
-- STEP 2 (only if STEP 0 was done before applying: put the old rows back, then drop the backup):
--   REPLACE INTO `gameobject` SELECT * FROM `bak_d3c1c14b_gameobject`;  DROP TABLE `bak_d3c1c14b_gameobject`;
--   REPLACE INTO `gameobject_template` SELECT * FROM `bak_d3c1c14b_gameobject_template`;  DROP TABLE `bak_d3c1c14b_gameobject_template`;
--   REPLACE INTO `pool_members` SELECT * FROM `bak_d3c1c14b_pool_members`;  DROP TABLE `bak_d3c1c14b_pool_members`;
--   REPLACE INTO `pool_template` SELECT * FROM `bak_d3c1c14b_pool_template`;  DROP TABLE `bak_d3c1c14b_pool_template`;
--   REPLACE INTO `quest_poi` SELECT * FROM `bak_d3c1c14b_quest_poi`;  DROP TABLE `bak_d3c1c14b_quest_poi`;
--   REPLACE INTO `quest_poi_points` SELECT * FROM `bak_d3c1c14b_quest_poi_points`;  DROP TABLE `bak_d3c1c14b_quest_poi_points`;
-- Afterwards `DELETE FROM updates WHERE name = '2026_11_16_59_world_local_clean_rows_from_replaced_files.sql';` if the file is to run again.

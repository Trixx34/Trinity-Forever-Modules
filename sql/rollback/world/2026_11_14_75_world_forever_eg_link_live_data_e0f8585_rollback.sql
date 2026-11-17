-- Rollback for sql/custom/world/2026_11_14_75_world_forever_eg_link_live_data_e0f8585.sql. NOT run by the updater: run it by hand on the world database (forever_*_world); restart the worldserver afterwards.
-- Generated from the file's own statements: the active DELETEs remove the rows the file wrote (same keys the file deletes before it inserts).
-- Rows the file itself DELETED or UPDATED cannot be restored exactly without a before-image. Take it BEFORE the file is applied (STEP 0), then use STEP 2.
--
-- STEP 0 (before applying the file, optional but needed for an exact undo of deleted/updated rows):
--   CREATE TABLE `bak_d14cd735_creature_template` AS SELECT * FROM `creature_template` WHERE (`entry`=4722) OR (`entry`=269127);
--   CREATE TABLE `bak_d14cd735_quest_objectives` AS SELECT * FROM `quest_objectives` WHERE (`ID`=90035050 AND `QuestID`=3505 AND `ObjectID`=6199) OR (`ID`=90035051 AND `QuestID`=3505 AND `ObjectID`=6198);
--   CREATE TABLE `bak_d14cd735_quest_template` AS SELECT * FROM `quest_template` WHERE (`ID`=3505) OR (`ID`=3517) OR (`ID`=6403) OR (`ID`=7790) OR (`ID`=7873) OR (`ID`=8080) OR (`ID`=8123) OR (`ID`=8152) OR (`ID`=8160) OR (`ID`=8367) OR (`ID`=8371) OR (`ID`=94413);
--   CREATE TABLE `bak_d14cd735_trainer_spell` AS SELECT * FROM `trainer_spell` WHERE (`TrainerId`=1002998 AND `SpellId`=2018);
--
-- STEP 1 (the undo that needs no before-image):
DELETE FROM `creature_questender` WHERE (`id`,`quest`) IN ((4722,79360),(269127,97287));
-- creature_template: only UPDATE in the file: nothing to remove; old values come back only through STEP 2.
DELETE FROM `gameobject_questitem` WHERE (`GameObjectEntry`,`Idx`) IN ((424002,0),(556653,0),(650929,0),(656100,0),(656101,0),(665153,0),(670348,0),(708078,0));
DELETE FROM `gameobject_template` WHERE `entry` IN (423910,424002,424003,424266,425896,425897,425899,556645,556653,556660,556678,630887,650929,656100,656101,657390,657572,657574,657575,657692,657693,657694,657695,657696,657697,657698,657699,657700,657701,657702,657703,657704,657705,657706,657707,657708,657709,657710,657711,657712,657713,657714,657715,657716,657717,657718,657719,657720,657721,657722,657723,657724,657725,657726,657727,657728,657729,657730,657731,657732,657733,657734,657735,657736,665153,670348,673054,673056,676051,707060,708078,716736,612130);
-- quest_objectives: only UPDATE in the file: nothing to remove; old values come back only through STEP 2.
-- quest_template: only UPDATE in the file: nothing to remove; old values come back only through STEP 2.
-- trainer_spell: only UPDATE in the file: nothing to remove; old values come back only through STEP 2.
--
-- STEP 2 (only if STEP 0 was done before applying: put the old rows back, then drop the backup):
--   REPLACE INTO `creature_template` SELECT * FROM `bak_d14cd735_creature_template`;  DROP TABLE `bak_d14cd735_creature_template`;
--   REPLACE INTO `quest_objectives` SELECT * FROM `bak_d14cd735_quest_objectives`;  DROP TABLE `bak_d14cd735_quest_objectives`;
--   REPLACE INTO `quest_template` SELECT * FROM `bak_d14cd735_quest_template`;  DROP TABLE `bak_d14cd735_quest_template`;
--   REPLACE INTO `trainer_spell` SELECT * FROM `bak_d14cd735_trainer_spell`;  DROP TABLE `bak_d14cd735_trainer_spell`;
-- Afterwards `DELETE FROM updates WHERE name = '2026_11_14_75_world_forever_eg_link_live_data_e0f8585.sql';` if the file is to run again.

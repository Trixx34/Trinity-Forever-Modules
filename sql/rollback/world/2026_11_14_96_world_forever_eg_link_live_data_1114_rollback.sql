-- Rollback for sql/custom/world/2026_11_14_96_world_forever_eg_link_live_data_1114.sql. NOT run by the updater: run it by hand on the world database (forever_*_world); restart the worldserver afterwards.
-- Generated from the file's own statements: the active DELETEs remove the rows the file wrote (same keys the file deletes before it inserts).
-- Rows the file itself DELETED or UPDATED cannot be restored exactly without a before-image. Take it BEFORE the file is applied (STEP 0), then use STEP 2.
--
-- STEP 0 (before applying the file, optional but needed for an exact undo of deleted/updated rows):
--   CREATE TABLE `bak_40ee71fb_creature_template_difficulty` AS SELECT * FROM `creature_template_difficulty` WHERE (`Entry`=1985 AND `DifficultyID`=0 AND `SkinLootID`=0);
--   CREATE TABLE `bak_40ee71fb_quest_objectives` AS SELECT * FROM `quest_objectives` WHERE (`ID` IN (383023,383135,383408,382899));
--   CREATE TABLE `bak_40ee71fb_quest_reward_display_spell` AS SELECT * FROM `quest_reward_display_spell` WHERE (`QuestID` IN (2771,7647));
--   CREATE TABLE `bak_40ee71fb_quest_template` AS SELECT * FROM `quest_template` WHERE (`ID`=8160);
--   CREATE TABLE `bak_40ee71fb_skinning_loot_template` AS SELECT * FROM `skinning_loot_template` WHERE (`Entry`=1985 AND `ItemType`=0 AND `Item`=2934);
--
-- STEP 1 (the undo that needs no before-image):
-- creature_template_difficulty: only UPDATE in the file: nothing to remove; old values come back only through STEP 2.
DELETE FROM `gameobject_questitem` WHERE (`GameObjectEntry`,`Idx`) IN ((424002,0));
DELETE FROM `gameobject_template` WHERE `entry` IN (424002,612130);
DELETE FROM `quest_classic_level` WHERE `ID` IN (98517);
DELETE FROM `quest_objectives` WHERE (`ID` IN (383023,383135,383408,382899));   -- the file's own delete condition: removes what it wrote
DELETE FROM `quest_reward_display_spell` WHERE (`QuestID` IN (2771,7647));   -- the file's own delete condition: removes what it wrote
DELETE FROM `quest_template` WHERE `ID` IN (98517);
DELETE FROM `quest_template_addon` WHERE `ID` IN (98517);
DELETE FROM `quest_template_classic_level` WHERE `ID` IN (98517);
DELETE FROM `skinning_loot_template` WHERE (`Entry`=1985 AND `ItemType`=0 AND `Item`=2934);   -- the file's own delete condition: removes what it wrote
--
-- STEP 2 (only if STEP 0 was done before applying: put the old rows back, then drop the backup):
--   REPLACE INTO `creature_template_difficulty` SELECT * FROM `bak_40ee71fb_creature_template_difficulty`;  DROP TABLE `bak_40ee71fb_creature_template_difficulty`;
--   REPLACE INTO `quest_objectives` SELECT * FROM `bak_40ee71fb_quest_objectives`;  DROP TABLE `bak_40ee71fb_quest_objectives`;
--   REPLACE INTO `quest_reward_display_spell` SELECT * FROM `bak_40ee71fb_quest_reward_display_spell`;  DROP TABLE `bak_40ee71fb_quest_reward_display_spell`;
--   REPLACE INTO `quest_template` SELECT * FROM `bak_40ee71fb_quest_template`;  DROP TABLE `bak_40ee71fb_quest_template`;
--   REPLACE INTO `skinning_loot_template` SELECT * FROM `bak_40ee71fb_skinning_loot_template`;  DROP TABLE `bak_40ee71fb_skinning_loot_template`;
-- Afterwards `DELETE FROM updates WHERE name = '2026_11_14_96_world_forever_eg_link_live_data_1114.sql';` if the file is to run again.

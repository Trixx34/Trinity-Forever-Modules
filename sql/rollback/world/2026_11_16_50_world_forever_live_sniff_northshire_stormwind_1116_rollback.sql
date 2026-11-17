-- Rollback for sql/custom/world/2026_11_16_50_world_forever_live_sniff_northshire_stormwind_1116.sql. NOT run by the updater: run it by hand on the world database (forever_*_world); restart the worldserver afterwards.
-- Generated from the file's own statements: the active DELETEs remove the rows the file wrote (same keys the file deletes before it inserts).
-- Rows the file itself DELETED or UPDATED cannot be restored exactly without a before-image. Take it BEFORE the file is applied (STEP 0), then use STEP 2.
--
-- STEP 0 (before applying the file, optional but needed for an exact undo of deleted/updated rows):
--   CREATE TABLE `bak_80814692_creature_equip_template` AS SELECT * FROM `creature_equip_template` WHERE (`ID` = 1 AND `CreatureID` = 1642);
--   CREATE TABLE `bak_80814692_creature_model_info` AS SELECT * FROM `creature_model_info` WHERE (`DisplayID` IN (139886, 127669, 127670, 129645, 137874));
--   CREATE TABLE `bak_80814692_creature_template` AS SELECT * FROM `creature_template` WHERE (`entry` = 7389) OR (`entry` = 248415) OR (`entry` = 251507) OR (`entry` = 80);
--   CREATE TABLE `bak_80814692_creature_template_model` AS SELECT * FROM `creature_template_model` WHERE (`CreatureID` = 721) OR (`CreatureID` = 257) OR (`CreatureID` = 6) OR (`CreatureID` = 80) OR (`CreatureID` = 2442) OR (`CreatureID` = 14721) OR (`CreatureID` = 3507) OR (`CreatureID` = 3508) OR (`CreatureID` = 3512) OR (`CreatureID` = 7559) OR (`CreatureID` = 38 AND `CreatureDisplayID` IN (129962, 129963));
--   CREATE TABLE `bak_80814692_gameobject_template` AS SELECT * FROM `gameobject_template` WHERE (`type` = 5 AND `entry` IN (696146, 696147, 696148, 696149, 696150, 696151, 696152, 696153, 696154, 696155, 696156, 696157, 696158, 696159, 696164, 696165, 696166, 696167, 696168, 696169, 696170, 696171, 696172, 696173, 696174, 696175, 696176, 696177, 696178, 696179, 696180, 696181, 696182, 696183, 696187, 696188, 696189, 696190, 696191, 696192, 696193, 696194, 696195, 696196, 696197, 696198, 696200, 696201, 696204, 696205, 696206, 696207, 696208, 696209, 696212, 696213, 696215, 696216, 696217, 696218, 696220, 696221, 696222, 696223, 696224, 696225, 696226, 696227, 696233, 696234, 696235, 696236, 696237, 696238, 696239, 696240, 696241, 696242, 696243, 696244, 696245, 696246, 696248, 696249, 696251, 696252, 696254, 696255, 696256, 696260, 696262, 696263, 696282, 696283, 696284, 696285, 696286, 696287, 696288, 696289, 696290, 696292, 696342, 696377, 696750, 696778, 696779, 696781, 696782, 696783, 696785, 696786, 696787, 696788, 696789, 696790, 696791)) OR (`entry` = 696200) OR (`entry` = 696213) OR (`entry` = 696224) OR (`entry` = 696212) OR (`entry` = 696201) OR (`entry` = 696223) OR (`entry` = 696211);
--   CREATE TABLE `bak_80814692_quest_offer_reward` AS SELECT * FROM `quest_offer_reward` WHERE (`ID` = 91741) OR (`ID` = 92124);
--
-- STEP 1 (the undo that needs no before-image):
DELETE FROM `creature_equip_template` WHERE (`ID` = 1 AND `CreatureID` = 1642);   -- the file's own delete condition: removes what it wrote
DELETE FROM `creature_model_info` WHERE (`DisplayID` IN (139886, 127669, 127670, 129645, 137874));   -- the file's own delete condition: removes what it wrote
-- creature_template: only UPDATE in the file: nothing to remove; old values come back only through STEP 2.
DELETE FROM `creature_template_model` WHERE (`CreatureID` = 721) OR (`CreatureID` = 257) OR (`CreatureID` = 6) OR (`CreatureID` = 80) OR (`CreatureID` = 2442) OR (`CreatureID` = 14721) OR (`CreatureID` = 3507) OR (`CreatureID` = 3508) OR (`CreatureID` = 3512) OR (`CreatureID` = 7559) OR (`CreatureID` = 38 AND `CreatureDisplayID` IN (129962, 129963));   -- the file's own delete condition: removes what it wrote
-- gameobject_template: only UPDATE in the file: nothing to remove; old values come back only through STEP 2.
-- quest_offer_reward: only UPDATE in the file: nothing to remove; old values come back only through STEP 2.
--
-- STEP 2 (only if STEP 0 was done before applying: put the old rows back, then drop the backup):
--   REPLACE INTO `creature_equip_template` SELECT * FROM `bak_80814692_creature_equip_template`;  DROP TABLE `bak_80814692_creature_equip_template`;
--   REPLACE INTO `creature_model_info` SELECT * FROM `bak_80814692_creature_model_info`;  DROP TABLE `bak_80814692_creature_model_info`;
--   REPLACE INTO `creature_template` SELECT * FROM `bak_80814692_creature_template`;  DROP TABLE `bak_80814692_creature_template`;
--   REPLACE INTO `creature_template_model` SELECT * FROM `bak_80814692_creature_template_model`;  DROP TABLE `bak_80814692_creature_template_model`;
--   REPLACE INTO `gameobject_template` SELECT * FROM `bak_80814692_gameobject_template`;  DROP TABLE `bak_80814692_gameobject_template`;
--   REPLACE INTO `quest_offer_reward` SELECT * FROM `bak_80814692_quest_offer_reward`;  DROP TABLE `bak_80814692_quest_offer_reward`;
-- Afterwards `DELETE FROM updates WHERE name = '2026_11_16_50_world_forever_live_sniff_northshire_stormwind_1116.sql';` if the file is to run again.

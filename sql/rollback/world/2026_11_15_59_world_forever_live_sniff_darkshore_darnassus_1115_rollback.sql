-- Rollback for sql/custom/world/2026_11_15_59_world_forever_live_sniff_darkshore_darnassus_1115.sql. NOT run by the updater: run it by hand on the world database (forever_*_world); restart the worldserver afterwards.
-- Generated from the file's own statements: the active DELETEs remove the rows the file wrote (same keys the file deletes before it inserts).
-- Rows the file itself DELETED or UPDATED cannot be restored exactly without a before-image. Take it BEFORE the file is applied (STEP 0), then use STEP 2.
--
-- STEP 0 (before applying the file, optional but needed for an exact undo of deleted/updated rows):
--   CREATE TABLE `bak_05ce7251_creature` AS SELECT * FROM `creature` WHERE (`guid` BETWEEN 46120000 AND 46120199);
--   CREATE TABLE `bak_05ce7251_creature_equip_template` AS SELECT * FROM `creature_equip_template` WHERE (`ID` = 1 AND `CreatureID` IN (4262, 6086, 274757, 274927, 263644, 271903));
--   CREATE TABLE `bak_05ce7251_creature_template` AS SELECT * FROM `creature_template` WHERE (`entry` IN (11519, 2212)) OR (`entry` = 12940) OR (`entry` = 11836) OR (`entry` = 3468) OR (`entry` = 270294) OR (`entry` IN (4235, 4185)) OR (`entry` = 274757) OR (`entry` = 274927) OR (`entry` = 274780) OR (`entry` = 270269) OR (`entry` = 263644) OR (`entry` = 271903) OR (`entry` = 270294) OR (`entry` = 261578) OR (`entry` = 261529) OR (`entry` = 261579) OR (`entry` = 276731) OR (`entry` = 265472) OR (`entry` = 276730) OR (`entry` = 276732);
--   CREATE TABLE `bak_05ce7251_creature_template_model` AS SELECT * FROM `creature_template_model` WHERE (`CreatureID` = 3694) OR (`CreatureID` = 2041) OR (`CreatureID` = 274757);
--   CREATE TABLE `bak_05ce7251_gameobject` AS SELECT * FROM `gameobject` WHERE (`guid` = 838001000 AND `id` = 630865) OR (`guid` BETWEEN 46120000 AND 46120199);
--   CREATE TABLE `bak_05ce7251_gameobject_template` AS SELECT * FROM `gameobject_template` WHERE (`entry` = 630866) OR (`entry` = 630867) OR (`entry` = 703896) OR (`entry` = 702763) OR (`entry` = 702830) OR (`entry` = 702832) OR (`entry` = 702833);
--   CREATE TABLE `bak_05ce7251_gameobject_template_locale` AS SELECT * FROM `gameobject_template_locale` WHERE (`locale` = 'ruRU' AND `entry` IN (630866, 630867, 703896, 702763, 702830, 702832, 702833));
--   CREATE TABLE `bak_05ce7251_npc_vendor` AS SELECT * FROM `npc_vendor` WHERE (`entry` = 4173 AND `item` = 287063);
--   CREATE TABLE `bak_05ce7251_quest_offer_reward_locale` AS SELECT * FROM `quest_offer_reward_locale` WHERE (`locale` = 'ruRU' AND `ID` = 951) OR (`locale` = 'ruRU' AND `ID` IN (667, 990)) OR (`locale` = 'ruRU' AND `ID` IN (729, 967, 1007, 1141, 2098, 2139, 5321, 87760, 98013, 98042));
--   CREATE TABLE `bak_05ce7251_quest_request_items_locale` AS SELECT * FROM `quest_request_items_locale` WHERE (`locale` = 'ruRU' AND `ID` IN (952, 1007, 2098, 4733, 7792, 87760, 98042));
--
-- STEP 1 (the undo that needs no before-image):
DELETE FROM `creature` WHERE (`guid` BETWEEN 46120000 AND 46120199);   -- the file's own delete condition: removes what it wrote
DELETE FROM `creature_equip_template` WHERE (`ID` = 1 AND `CreatureID` IN (4262, 6086, 274757, 274927, 263644, 271903));   -- the file's own delete condition: removes what it wrote
-- creature_template: only UPDATE in the file: nothing to remove; old values come back only through STEP 2.
-- creature_template_addon: the file inserts rows that are not listed with their keys (INSERT ... SELECT / ON DUPLICATE KEY). Exact undo needs the before-image:
--   STEP 0: CREATE TABLE `bak_05ce7251_creature_template_addon` AS SELECT * FROM `creature_template_addon`;   (full copy, before the file is applied)
--   STEP 1b: DELETE x FROM `creature_template_addon` x LEFT JOIN `bak_05ce7251_creature_template_addon` b ON x.`entry` = b.`entry` WHERE b.`entry` IS NULL;   (rows the file added)
--   STEP 2: REPLACE INTO `creature_template_addon` SELECT * FROM `bak_05ce7251_creature_template_addon`;  DROP TABLE `bak_05ce7251_creature_template_addon`;   (changed rows back)
DELETE FROM `creature_template_model` WHERE (`CreatureID` = 3694) OR (`CreatureID` = 2041) OR (`CreatureID` = 274757);   -- the file's own delete condition: removes what it wrote
DELETE FROM `gameobject` WHERE (`guid` = 838001000 AND `id` = 630865) OR (`guid` BETWEEN 46120000 AND 46120199);   -- the file's own delete condition: removes what it wrote
DELETE FROM `gameobject_template` WHERE (`entry` = 630866) OR (`entry` = 630867) OR (`entry` = 703896) OR (`entry` = 702763) OR (`entry` = 702830) OR (`entry` = 702832) OR (`entry` = 702833);   -- the file's own delete condition: removes what it wrote
DELETE FROM `gameobject_template_locale` WHERE (`locale` = 'ruRU' AND `entry` IN (630866, 630867, 703896, 702763, 702830, 702832, 702833));   -- the file's own delete condition: removes what it wrote
DELETE FROM `npc_vendor` WHERE (`entry` = 4173 AND `item` = 287063);   -- the file's own delete condition: removes what it wrote
DELETE FROM `quest_offer_reward_locale` WHERE (`locale` = 'ruRU' AND `ID` IN (729, 967, 1007, 1141, 2098, 2139, 5321, 87760, 98013, 98042));   -- the file's own delete condition: removes what it wrote
DELETE FROM `quest_request_items_locale` WHERE (`locale` = 'ruRU' AND `ID` IN (952, 1007, 2098, 4733, 7792, 87760, 98042));   -- the file's own delete condition: removes what it wrote
--
-- STEP 2 (only if STEP 0 was done before applying: put the old rows back, then drop the backup):
--   REPLACE INTO `creature` SELECT * FROM `bak_05ce7251_creature`;  DROP TABLE `bak_05ce7251_creature`;
--   REPLACE INTO `creature_equip_template` SELECT * FROM `bak_05ce7251_creature_equip_template`;  DROP TABLE `bak_05ce7251_creature_equip_template`;
--   REPLACE INTO `creature_template` SELECT * FROM `bak_05ce7251_creature_template`;  DROP TABLE `bak_05ce7251_creature_template`;
--   REPLACE INTO `creature_template_model` SELECT * FROM `bak_05ce7251_creature_template_model`;  DROP TABLE `bak_05ce7251_creature_template_model`;
--   REPLACE INTO `gameobject` SELECT * FROM `bak_05ce7251_gameobject`;  DROP TABLE `bak_05ce7251_gameobject`;
--   REPLACE INTO `gameobject_template` SELECT * FROM `bak_05ce7251_gameobject_template`;  DROP TABLE `bak_05ce7251_gameobject_template`;
--   REPLACE INTO `gameobject_template_locale` SELECT * FROM `bak_05ce7251_gameobject_template_locale`;  DROP TABLE `bak_05ce7251_gameobject_template_locale`;
--   REPLACE INTO `npc_vendor` SELECT * FROM `bak_05ce7251_npc_vendor`;  DROP TABLE `bak_05ce7251_npc_vendor`;
--   REPLACE INTO `quest_offer_reward_locale` SELECT * FROM `bak_05ce7251_quest_offer_reward_locale`;  DROP TABLE `bak_05ce7251_quest_offer_reward_locale`;
--   REPLACE INTO `quest_request_items_locale` SELECT * FROM `bak_05ce7251_quest_request_items_locale`;  DROP TABLE `bak_05ce7251_quest_request_items_locale`;
-- Afterwards `DELETE FROM updates WHERE name = '2026_11_15_59_world_forever_live_sniff_darkshore_darnassus_1115.sql';` if the file is to run again.

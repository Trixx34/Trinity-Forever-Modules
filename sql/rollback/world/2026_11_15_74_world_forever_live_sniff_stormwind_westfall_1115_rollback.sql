-- Rollback for sql/custom/world/2026_11_15_74_world_forever_live_sniff_stormwind_westfall_1115.sql. NOT run by the updater: run it by hand on the world database (forever_*_world); restart the worldserver afterwards.
-- Generated from the file's own statements: the active DELETEs remove the rows the file wrote (same keys the file deletes before it inserts).
-- Rows the file itself DELETED or UPDATED cannot be restored exactly without a before-image. Take it BEFORE the file is applied (STEP 0), then use STEP 2.
--
-- STEP 0 (before applying the file, optional but needed for an exact undo of deleted/updated rows):
--   CREATE TABLE `bak_5fda3684_creature_equip_template` AS SELECT * FROM `creature_equip_template` WHERE (`ID` = 1 AND `CreatureID` IN (68, 487, 488, 489, 490, 869, 870, 874, 876, 1423, 1756, 1976, 249215, 253092, 267007, 267064, 267118, 274906));
--   CREATE TABLE `bak_5fda3684_creature_template` AS SELECT * FROM `creature_template` WHERE (`entry` = 5492) OR (`entry` = 10598) OR (`entry` IN (12782, 12777)) OR (`entry` = 253395) OR (`entry` = 253092) OR (`entry` = 4961) OR (`entry` = 267007) OR (`entry` = 267064) OR (`entry` = 249215);
--   CREATE TABLE `bak_5fda3684_creature_template_model` AS SELECT * FROM `creature_template_model` WHERE (`CreatureID` = 113) OR (`CreatureID` = 2532) OR (`CreatureID` = 14496) OR (`CreatureID` = 267007) OR (`CreatureID` = 267064) OR (`CreatureID` = 249215) OR (`CreatureID` = 10598);
--   CREATE TABLE `bak_5fda3684_gameobject` AS SELECT * FROM `gameobject` WHERE (`guid` BETWEEN 46120200 AND 46120399);
--   CREATE TABLE `bak_5fda3684_gameobject_template` AS SELECT * FROM `gameobject_template` WHERE (`entry` = 177666) OR (`entry` = 109515) OR (`entry` = 696211) OR (`entry` = 696293) OR (`entry` = 696294) OR (`entry` = 106529) OR (`entry` IN (660672, 660709, 696274, 696608, 696609, 696610, 696611, 696612, 696613, 696614, 696615, 696616, 696617, 696618, 696619, 696620, 696621, 696622, 696623, 696624, 696625, 696626, 696627, 696628, 696629, 696630, 696631, 696632, 696633, 696634, 696635, 696636, 696637, 696638, 696639, 696640, 696641, 696642, 696643, 696644, 696645, 696646, 696647, 696648, 696649, 696650, 696651, 696652));
--   CREATE TABLE `bak_5fda3684_npc_vendor` AS SELECT * FROM `npc_vendor` WHERE (`entry` = 4200 AND `item` = 6303);
--   CREATE TABLE `bak_5fda3684_quest_offer_reward` AS SELECT * FROM `quest_offer_reward` WHERE (`ID` = 98025);
--   CREATE TABLE `bak_5fda3684_quest_request_items` AS SELECT * FROM `quest_request_items` WHERE (`ID` = 98025);
--
-- STEP 1 (the undo that needs no before-image):
DELETE FROM `creature_equip_template` WHERE (`ID` = 1 AND `CreatureID` IN (68, 487, 488, 489, 490, 869, 870, 874, 876, 1423, 1756, 1976, 249215, 253092, 267007, 267064, 267118, 274906));   -- the file's own delete condition: removes what it wrote
-- creature_template: only UPDATE in the file: nothing to remove; old values come back only through STEP 2.
-- creature_template_addon: the file inserts rows that are not listed with their keys (INSERT ... SELECT / ON DUPLICATE KEY). Exact undo needs the before-image:
--   STEP 0: CREATE TABLE `bak_5fda3684_creature_template_addon` AS SELECT * FROM `creature_template_addon`;   (full copy, before the file is applied)
--   STEP 1b: DELETE x FROM `creature_template_addon` x LEFT JOIN `bak_5fda3684_creature_template_addon` b ON x.`entry` = b.`entry` WHERE b.`entry` IS NULL;   (rows the file added)
--   STEP 2: REPLACE INTO `creature_template_addon` SELECT * FROM `bak_5fda3684_creature_template_addon`;  DROP TABLE `bak_5fda3684_creature_template_addon`;   (changed rows back)
DELETE FROM `creature_template_model` WHERE (`CreatureID` = 113) OR (`CreatureID` = 2532) OR (`CreatureID` = 14496) OR (`CreatureID` = 267007) OR (`CreatureID` = 267064) OR (`CreatureID` = 249215) OR (`CreatureID` = 10598);   -- the file's own delete condition: removes what it wrote
DELETE FROM `gameobject` WHERE (`guid` BETWEEN 46120200 AND 46120399);   -- the file's own delete condition: removes what it wrote
DELETE FROM `gameobject_template` WHERE (`entry` IN (660672, 660709, 696274, 696608, 696609, 696610, 696611, 696612, 696613, 696614, 696615, 696616, 696617, 696618, 696619, 696620, 696621, 696622, 696623, 696624, 696625, 696626, 696627, 696628, 696629, 696630, 696631, 696632, 696633, 696634, 696635, 696636, 696637, 696638, 696639, 696640, 696641, 696642, 696643, 696644, 696645, 696646, 696647, 696648, 696649, 696650, 696651, 696652));   -- the file's own delete condition: removes what it wrote
DELETE FROM `npc_vendor` WHERE (`entry` = 4200 AND `item` = 6303);   -- the file's own delete condition: removes what it wrote
-- quest_offer_reward: only UPDATE in the file: nothing to remove; old values come back only through STEP 2.
-- quest_request_items: only UPDATE in the file: nothing to remove; old values come back only through STEP 2.
--
-- STEP 2 (only if STEP 0 was done before applying: put the old rows back, then drop the backup):
--   REPLACE INTO `creature_equip_template` SELECT * FROM `bak_5fda3684_creature_equip_template`;  DROP TABLE `bak_5fda3684_creature_equip_template`;
--   REPLACE INTO `creature_template` SELECT * FROM `bak_5fda3684_creature_template`;  DROP TABLE `bak_5fda3684_creature_template`;
--   REPLACE INTO `creature_template_model` SELECT * FROM `bak_5fda3684_creature_template_model`;  DROP TABLE `bak_5fda3684_creature_template_model`;
--   REPLACE INTO `gameobject` SELECT * FROM `bak_5fda3684_gameobject`;  DROP TABLE `bak_5fda3684_gameobject`;
--   REPLACE INTO `gameobject_template` SELECT * FROM `bak_5fda3684_gameobject_template`;  DROP TABLE `bak_5fda3684_gameobject_template`;
--   REPLACE INTO `npc_vendor` SELECT * FROM `bak_5fda3684_npc_vendor`;  DROP TABLE `bak_5fda3684_npc_vendor`;
--   REPLACE INTO `quest_offer_reward` SELECT * FROM `bak_5fda3684_quest_offer_reward`;  DROP TABLE `bak_5fda3684_quest_offer_reward`;
--   REPLACE INTO `quest_request_items` SELECT * FROM `bak_5fda3684_quest_request_items`;  DROP TABLE `bak_5fda3684_quest_request_items`;
-- Afterwards `DELETE FROM updates WHERE name = '2026_11_15_74_world_forever_live_sniff_stormwind_westfall_1115.sql';` if the file is to run again.

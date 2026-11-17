-- Rollback for sql/custom/world/2026_11_15_54_world_forever_live_sniff_westfall_zephras_1115.sql. NOT run by the updater: run it by hand on the world database (forever_*_world); restart the worldserver afterwards.
-- Generated from the file's own statements: the active DELETEs remove the rows the file wrote (same keys the file deletes before it inserts).
-- Rows the file itself DELETED or UPDATED cannot be restored exactly without a before-image. Take it BEFORE the file is applied (STEP 0), then use STEP 2.
--
-- STEP 0 (before applying the file, optional but needed for an exact undo of deleted/updated rows):
--   CREATE TABLE `bak_954a948f_creature_loot_template` AS SELECT * FROM `creature_loot_template` WHERE (`Comment` LIKE 'Forever live drop 2026-11-15 (%');
--   CREATE TABLE `bak_954a948f_gameobject_loot_template` AS SELECT * FROM `gameobject_loot_template` WHERE (`Comment` LIKE 'Forever live drop 2026-11-15 (%');
--   CREATE TABLE `bak_954a948f_npc_vendor` AS SELECT * FROM `npc_vendor` WHERE ((`entry`,`item`) IN ((257422,287063),(252449,4536),(252449,277115),(252449,277114),(252449,277113), (257006,2692),(257006,3713),(257006,10284),(257006,4471),(257006,4470),(257006,11291),(257006,272941),(257006,21099),(257006,21219)));
--
-- STEP 1 (the undo that needs no before-image):
DELETE FROM `creature_loot_template` WHERE (`Comment` LIKE 'Forever live drop 2026-11-15 (%');   -- the file's own delete condition: removes what it wrote
DELETE FROM `gameobject_loot_template` WHERE (`Comment` LIKE 'Forever live drop 2026-11-15 (%');   -- the file's own delete condition: removes what it wrote
DELETE FROM `npc_vendor` WHERE ((`entry`,`item`) IN ((257422,287063),(252449,4536),(252449,277115),(252449,277114),(252449,277113), (257006,2692),(257006,3713),(257006,10284),(257006,4471),(257006,4470),(257006,11291),(257006,272941),(257006,21099),(257006,21219)));   -- the file's own delete condition: removes what it wrote
-- quest_offer_reward: the file inserts rows that are not listed with their keys (INSERT ... SELECT / ON DUPLICATE KEY). Exact undo needs the before-image:
--   STEP 0: CREATE TABLE `bak_954a948f_quest_offer_reward` AS SELECT * FROM `quest_offer_reward`;   (full copy, before the file is applied)
--   STEP 1b: DELETE x FROM `quest_offer_reward` x LEFT JOIN `bak_954a948f_quest_offer_reward` b ON x.`ID` = b.`ID` WHERE b.`ID` IS NULL;   (rows the file added)
--   STEP 2: REPLACE INTO `quest_offer_reward` SELECT * FROM `bak_954a948f_quest_offer_reward`;  DROP TABLE `bak_954a948f_quest_offer_reward`;   (changed rows back)
--
-- STEP 2 (only if STEP 0 was done before applying: put the old rows back, then drop the backup):
--   REPLACE INTO `creature_loot_template` SELECT * FROM `bak_954a948f_creature_loot_template`;  DROP TABLE `bak_954a948f_creature_loot_template`;
--   REPLACE INTO `gameobject_loot_template` SELECT * FROM `bak_954a948f_gameobject_loot_template`;  DROP TABLE `bak_954a948f_gameobject_loot_template`;
--   REPLACE INTO `npc_vendor` SELECT * FROM `bak_954a948f_npc_vendor`;  DROP TABLE `bak_954a948f_npc_vendor`;
-- Afterwards `DELETE FROM updates WHERE name = '2026_11_15_54_world_forever_live_sniff_westfall_zephras_1115.sql';` if the file is to run again.

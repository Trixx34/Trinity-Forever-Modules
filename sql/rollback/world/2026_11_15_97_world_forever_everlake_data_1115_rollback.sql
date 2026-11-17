-- Rollback for sql/custom/world/2026_11_15_97_world_forever_everlake_data_1115.sql. NOT run by the updater: run it by hand on the world database (forever_*_world); restart the worldserver afterwards.
-- Generated from the file's own statements: the active DELETEs remove the rows the file wrote (same keys the file deletes before it inserts).
-- Rows the file itself DELETED or UPDATED cannot be restored exactly without a before-image. Take it BEFORE the file is applied (STEP 0), then use STEP 2.
--
-- STEP 0 (before applying the file, optional but needed for an exact undo of deleted/updated rows):
--   CREATE TABLE `bak_5bb077d0_creature_questender` AS SELECT * FROM `creature_questender` WHERE ((`id`,`quest`) IN ((4606,5659),(11216,5383),(14733,8290),(14781,8295)));
--   CREATE TABLE `bak_5bb077d0_creature_template_difficulty` AS SELECT * FROM `creature_template_difficulty` WHERE (`d`.`DifficultyID`=0 AND `d`.`LootID`=0 AND `d`.`Entry` IN (SELECT `Entry` FROM `forever_drops_1115`) AND NOT EXISTS (SELECT 1 FROM `creature_loot_template` `l` WHERE `l`.`Entry`=`d`.`Entry`));
--   CREATE TABLE `bak_5bb077d0_gameobject_loot_template` AS SELECT * FROM `gameobject_loot_template` WHERE (`ItemType`=0 AND (`Entry`,`Item`) IN ((1417,268416),(1418,268416),(1419,268416),(1420,268416),(1421,268416),(1423,268417),(1424,268416),(1730,268417),(1731,268417),(1733,268416),(1734,268418),(6142,268418),(6152,268418),(137445,260927),(137455,260927),(137456,260927),(139192,252023),(140476,271629),(142369,278029)));
--   CREATE TABLE `bak_5bb077d0_npc_vendor` AS SELECT * FROM `npc_vendor` WHERE (`ExtendedCost`=0 AND `type`=1 AND (`entry`,`item`) IN ((265346,274616),(265347,278118),(265347,278120),(265347,278122)));
--
-- STEP 1 (the undo that needs no before-image):
-- creature_loot_template: the file inserts rows that are not listed with their keys (INSERT ... SELECT / ON DUPLICATE KEY). Exact undo needs the before-image:
--   STEP 0: CREATE TABLE `bak_5bb077d0_creature_loot_template` AS SELECT * FROM `creature_loot_template`;   (full copy, before the file is applied)
--   STEP 1b: DELETE x FROM `creature_loot_template` x LEFT JOIN `bak_5bb077d0_creature_loot_template` b ON x.`Entry` = b.`Entry` AND x.`ItemType` = b.`ItemType` AND x.`Item` = b.`Item` WHERE b.`Entry` IS NULL;   (rows the file added)
--   STEP 2: REPLACE INTO `creature_loot_template` SELECT * FROM `bak_5bb077d0_creature_loot_template`;  DROP TABLE `bak_5bb077d0_creature_loot_template`;   (changed rows back)
DELETE FROM `creature_questender` WHERE ((`id`,`quest`) IN ((4606,5659),(11216,5383),(14733,8290),(14781,8295)));   -- the file's own delete condition: removes what it wrote
-- creature_template_difficulty: only UPDATE in the file: nothing to remove; old values come back only through STEP 2.
DELETE FROM `gameobject_loot_template` WHERE (`ItemType`=0 AND (`Entry`,`Item`) IN ((1417,268416),(1418,268416),(1419,268416),(1420,268416),(1421,268416),(1423,268417),(1424,268416),(1730,268417),(1731,268417),(1733,268416),(1734,268418),(6142,268418),(6152,268418),(137445,260927),(137455,260927),(137456,260927),(139192,252023),(140476,271629),(142369,278029)));   -- the file's own delete condition: removes what it wrote
DELETE FROM `npc_vendor` WHERE (`ExtendedCost`=0 AND `type`=1 AND (`entry`,`item`) IN ((265346,274616),(265347,278118),(265347,278120),(265347,278122)));   -- the file's own delete condition: removes what it wrote
--
-- STEP 2 (only if STEP 0 was done before applying: put the old rows back, then drop the backup):
--   REPLACE INTO `creature_questender` SELECT * FROM `bak_5bb077d0_creature_questender`;  DROP TABLE `bak_5bb077d0_creature_questender`;
--   REPLACE INTO `creature_template_difficulty` SELECT * FROM `bak_5bb077d0_creature_template_difficulty`;  DROP TABLE `bak_5bb077d0_creature_template_difficulty`;
--   REPLACE INTO `gameobject_loot_template` SELECT * FROM `bak_5bb077d0_gameobject_loot_template`;  DROP TABLE `bak_5bb077d0_gameobject_loot_template`;
--   REPLACE INTO `npc_vendor` SELECT * FROM `bak_5bb077d0_npc_vendor`;  DROP TABLE `bak_5bb077d0_npc_vendor`;
-- Afterwards `DELETE FROM updates WHERE name = '2026_11_15_97_world_forever_everlake_data_1115.sql';` if the file is to run again.

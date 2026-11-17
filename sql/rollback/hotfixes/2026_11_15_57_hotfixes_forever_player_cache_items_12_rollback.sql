-- Rollback for sql/custom/hotfixes/2026_11_15_57_hotfixes_forever_player_cache_items_12.sql. NOT run by the updater: run it by hand on the hotfixes database (forever_*_hotfixes); restart the worldserver afterwards.
-- Clients that cached the changed records keep them until their hotfix cache is cleared.
-- Generated from the file's own statements: the active DELETEs remove the rows the file wrote (same keys the file deletes before it inserts).
-- Rows the file itself DELETED or UPDATED cannot be restored exactly without a before-image. Take it BEFORE the file is applied (STEP 0), then use STEP 2.
--
-- STEP 0 (before applying the file, optional but needed for an exact undo of deleted/updated rows):
--   CREATE TABLE `bak_f3d8dc7d_hotfix_data` AS SELECT * FROM `hotfix_data` WHERE (`Id` BETWEEN 2096000 AND 2096999) OR ((`TableHash`,`RecordId`) IN ((1344507586,18674),(1344507586,22329),(1344507586,22333),(2442913102,18674),(2442913102,22329),(2442913102,22333)));
--   CREATE TABLE `bak_f3d8dc7d_item` AS SELECT * FROM `item` WHERE (`ID` IN (18674,22329,22333));
--   CREATE TABLE `bak_f3d8dc7d_item_sparse` AS SELECT * FROM `item_sparse` WHERE (`ID` IN (18674,22329,22333));
--
-- STEP 1 (the undo that needs no before-image):
DELETE FROM `hotfix_data` WHERE (`Id` BETWEEN 2096000 AND 2096999) OR ((`TableHash`,`RecordId`) IN ((1344507586,18674),(1344507586,22329),(1344507586,22333),(2442913102,18674),(2442913102,22329),(2442913102,22333)));   -- the file's own delete condition: removes what it wrote
DELETE FROM `item` WHERE (`ID` IN (18674,22329,22333));   -- the file's own delete condition: removes what it wrote
DELETE FROM `item_sparse` WHERE (`ID` IN (18674,22329,22333));   -- the file's own delete condition: removes what it wrote
--
-- STEP 2 (only if STEP 0 was done before applying: put the old rows back, then drop the backup):
--   REPLACE INTO `hotfix_data` SELECT * FROM `bak_f3d8dc7d_hotfix_data`;  DROP TABLE `bak_f3d8dc7d_hotfix_data`;
--   REPLACE INTO `item` SELECT * FROM `bak_f3d8dc7d_item`;  DROP TABLE `bak_f3d8dc7d_item`;
--   REPLACE INTO `item_sparse` SELECT * FROM `bak_f3d8dc7d_item_sparse`;  DROP TABLE `bak_f3d8dc7d_item_sparse`;
-- Afterwards `DELETE FROM updates WHERE name = '2026_11_15_57_hotfixes_forever_player_cache_items_12.sql';` if the file is to run again.

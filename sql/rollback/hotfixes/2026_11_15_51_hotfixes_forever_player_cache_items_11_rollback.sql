-- Rollback for sql/custom/hotfixes/2026_11_15_51_hotfixes_forever_player_cache_items_11.sql. NOT run by the updater: run it by hand on the hotfixes database (forever_*_hotfixes); restart the worldserver afterwards.
-- Clients that cached the changed records keep them until their hotfix cache is cleared.
-- Generated from the file's own statements: the active DELETEs remove the rows the file wrote (same keys the file deletes before it inserts).
-- Rows the file itself DELETED or UPDATED cannot be restored exactly without a before-image. Take it BEFORE the file is applied (STEP 0), then use STEP 2.
--
-- STEP 0 (before applying the file, optional but needed for an exact undo of deleted/updated rows):
--   CREATE TABLE `bak_fca54246_hotfix_data` AS SELECT * FROM `hotfix_data` WHERE (`Id` BETWEEN 2094000 AND 2094999) OR ((`TableHash`,`RecordId`) IN ((1344507586,18676),(1344507586,18677),(1344507586,18754),(1344507586,18755),(1344507586,18757),(1344507586,22325),(1344507586,22332),(1344507586,22335),(1344507586,22339),(2442913102,18676),(2442913102,18677),(2442913102,18754),(2442913102,18755),(2442913102,18757),(2442913102,22325),(2442913102,22332),(2442913102,22335),(2442913102,22339)));
--   CREATE TABLE `bak_fca54246_item` AS SELECT * FROM `item` WHERE (`ID` IN (18676,18677,18754,18755,18757,22325,22332,22335,22339));
--   CREATE TABLE `bak_fca54246_item_sparse` AS SELECT * FROM `item_sparse` WHERE (`ID` IN (18676,18677,18754,18755,18757,22325,22332,22335,22339));
--
-- STEP 1 (the undo that needs no before-image):
DELETE FROM `hotfix_data` WHERE (`Id` BETWEEN 2094000 AND 2094999) OR ((`TableHash`,`RecordId`) IN ((1344507586,18676),(1344507586,18677),(1344507586,18754),(1344507586,18755),(1344507586,18757),(1344507586,22325),(1344507586,22332),(1344507586,22335),(1344507586,22339),(2442913102,18676),(2442913102,18677),(2442913102,18754),(2442913102,18755),(2442913102,18757),(2442913102,22325),(2442913102,22332),(2442913102,22335),(2442913102,22339)));   -- the file's own delete condition: removes what it wrote
DELETE FROM `item` WHERE (`ID` IN (18676,18677,18754,18755,18757,22325,22332,22335,22339));   -- the file's own delete condition: removes what it wrote
DELETE FROM `item_sparse` WHERE (`ID` IN (18676,18677,18754,18755,18757,22325,22332,22335,22339));   -- the file's own delete condition: removes what it wrote
--
-- STEP 2 (only if STEP 0 was done before applying: put the old rows back, then drop the backup):
--   REPLACE INTO `hotfix_data` SELECT * FROM `bak_fca54246_hotfix_data`;  DROP TABLE `bak_fca54246_hotfix_data`;
--   REPLACE INTO `item` SELECT * FROM `bak_fca54246_item`;  DROP TABLE `bak_fca54246_item`;
--   REPLACE INTO `item_sparse` SELECT * FROM `bak_fca54246_item_sparse`;  DROP TABLE `bak_fca54246_item_sparse`;
-- Afterwards `DELETE FROM updates WHERE name = '2026_11_15_51_hotfixes_forever_player_cache_items_11.sql';` if the file is to run again.

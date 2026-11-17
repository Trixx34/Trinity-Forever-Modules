-- Rollback for sql/custom/hotfixes/2026_11_14_56_hotfixes_forever_player_cache_items_10.sql. NOT run by the updater: run it by hand on the hotfixes database (forever_*_hotfixes); restart the worldserver afterwards.
-- Clients that cached the changed records keep them until their hotfix cache is cleared.
-- Generated from the file's own statements: the active DELETEs remove the rows the file wrote (same keys the file deletes before it inserts).
-- Rows the file itself DELETED or UPDATED cannot be restored exactly without a before-image. Take it BEFORE the file is applied (STEP 0), then use STEP 2.
--
-- STEP 0 (before applying the file, optional but needed for an exact undo of deleted/updated rows):
--   CREATE TABLE `bak_721220bb_hotfix_data` AS SELECT * FROM `hotfix_data` WHERE (`Id` BETWEEN 2092000 AND 2092999) OR ((`TableHash`,`RecordId`) IN ((1344507586,22322),(1344507586,22327),(1344507586,22328),(1344507586,22334),(1344507586,217716),(1344507586,284163),(1344507586,284164),(2442913102,22322),(2442913102,22327),(2442913102,22328),(2442913102,22334),(2442913102,217716),(2442913102,284163),(2442913102,284164)));
--   CREATE TABLE `bak_721220bb_item` AS SELECT * FROM `item` WHERE (`ID` IN (22322,22327,22328,22334,217716,284163,284164));
--   CREATE TABLE `bak_721220bb_item_sparse` AS SELECT * FROM `item_sparse` WHERE (`ID` IN (22322,22327,22328,22334,217716,284163,284164));
--
-- STEP 1 (the undo that needs no before-image):
DELETE FROM `hotfix_data` WHERE (`Id` BETWEEN 2092000 AND 2092999) OR ((`TableHash`,`RecordId`) IN ((1344507586,22322),(1344507586,22327),(1344507586,22328),(1344507586,22334),(1344507586,217716),(1344507586,284163),(1344507586,284164),(2442913102,22322),(2442913102,22327),(2442913102,22328),(2442913102,22334),(2442913102,217716),(2442913102,284163),(2442913102,284164)));   -- the file's own delete condition: removes what it wrote
DELETE FROM `item` WHERE (`ID` IN (22322,22327,22328,22334,217716,284163,284164));   -- the file's own delete condition: removes what it wrote
DELETE FROM `item_sparse` WHERE (`ID` IN (22322,22327,22328,22334,217716,284163,284164));   -- the file's own delete condition: removes what it wrote
--
-- STEP 2 (only if STEP 0 was done before applying: put the old rows back, then drop the backup):
--   REPLACE INTO `hotfix_data` SELECT * FROM `bak_721220bb_hotfix_data`;  DROP TABLE `bak_721220bb_hotfix_data`;
--   REPLACE INTO `item` SELECT * FROM `bak_721220bb_item`;  DROP TABLE `bak_721220bb_item`;
--   REPLACE INTO `item_sparse` SELECT * FROM `bak_721220bb_item_sparse`;  DROP TABLE `bak_721220bb_item_sparse`;
-- Afterwards `DELETE FROM updates WHERE name = '2026_11_14_56_hotfixes_forever_player_cache_items_10.sql';` if the file is to run again.

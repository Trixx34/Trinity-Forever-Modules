-- Rollback for sql/custom/hotfixes/2026_11_15_70_hotfixes_classic_naxxramas_weapons.sql. NOT run by the updater: run it by hand on the hotfixes database (forever_*_hotfixes); restart the worldserver afterwards.
-- Clients that cached the changed records keep them until their hotfix cache is cleared.
-- Generated from the file's own statements: the active DELETEs remove the rows the file wrote (same keys the file deletes before it inserts).
-- Rows the file itself DELETED or UPDATED cannot be restored exactly without a before-image. Take it BEFORE the file is applied (STEP 0), then use STEP 2.
--
-- STEP 0 (before applying the file, optional but needed for an exact undo of deleted/updated rows):
--   CREATE TABLE `bak_d16c48a5_hotfix_data` AS SELECT * FROM `hotfix_data` WHERE (`TableHash` = 2442913102 AND `RecordId` IN (22691,22798,22799,22800,22801,22802,22803,22804,22806,22807,22808,22809,22810,22811,22812,22813,22815,22816,22818,22819,22820,22821,22937,22942,22988,22994,23009,23014,23029,23039,23044,23048,23049,23054,23056,23075,23221,23238,23242)) OR ((`TableHash` = 1073915313 OR `TableHash` = 13330255) AND `RecordId` IN (9910201,9910202,9910203,9910204,9910205));
--   CREATE TABLE `bak_d16c48a5_item_effect` AS SELECT * FROM `item_effect` WHERE (`ID` IN (9910201,9910202,9910203,9910204,9910205));
--   CREATE TABLE `bak_d16c48a5_item_sparse` AS SELECT * FROM `item_sparse` WHERE (`ID` IN (22691,22798,22799,22800,22801,22802,22803,22804,22806,22807,22808,22809,22810,22811,22812,22813,22815,22816,22818,22819,22820,22821,22937,22942,22988,22994,23009,23014,23029,23039,23044,23048,23049,23054,23056,23075,23221,23238,23242));
--   CREATE TABLE `bak_d16c48a5_item_x_item_effect` AS SELECT * FROM `item_x_item_effect` WHERE (`ID` IN (9910201,9910202,9910203,9910204,9910205));
--
-- STEP 1 (the undo that needs no before-image):
DELETE FROM `hotfix_data` WHERE (`TableHash` = 2442913102 AND `RecordId` IN (22691,22798,22799,22800,22801,22802,22803,22804,22806,22807,22808,22809,22810,22811,22812,22813,22815,22816,22818,22819,22820,22821,22937,22942,22988,22994,23009,23014,23029,23039,23044,23048,23049,23054,23056,23075,23221,23238,23242)) OR ((`TableHash` = 1073915313 OR `TableHash` = 13330255) AND `RecordId` IN (9910201,9910202,9910203,9910204,9910205));   -- the file's own delete condition: removes what it wrote
DELETE FROM `item_effect` WHERE (`ID` IN (9910201,9910202,9910203,9910204,9910205));   -- the file's own delete condition: removes what it wrote
DELETE FROM `item_sparse` WHERE (`ID` IN (22691,22798,22799,22800,22801,22802,22803,22804,22806,22807,22808,22809,22810,22811,22812,22813,22815,22816,22818,22819,22820,22821,22937,22942,22988,22994,23009,23014,23029,23039,23044,23048,23049,23054,23056,23075,23221,23238,23242));   -- the file's own delete condition: removes what it wrote
DELETE FROM `item_x_item_effect` WHERE (`ID` IN (9910201,9910202,9910203,9910204,9910205));   -- the file's own delete condition: removes what it wrote
--
-- STEP 2 (only if STEP 0 was done before applying: put the old rows back, then drop the backup):
--   REPLACE INTO `hotfix_data` SELECT * FROM `bak_d16c48a5_hotfix_data`;  DROP TABLE `bak_d16c48a5_hotfix_data`;
--   REPLACE INTO `item_effect` SELECT * FROM `bak_d16c48a5_item_effect`;  DROP TABLE `bak_d16c48a5_item_effect`;
--   REPLACE INTO `item_sparse` SELECT * FROM `bak_d16c48a5_item_sparse`;  DROP TABLE `bak_d16c48a5_item_sparse`;
--   REPLACE INTO `item_x_item_effect` SELECT * FROM `bak_d16c48a5_item_x_item_effect`;  DROP TABLE `bak_d16c48a5_item_x_item_effect`;
-- Afterwards `DELETE FROM updates WHERE name = '2026_11_15_70_hotfixes_classic_naxxramas_weapons.sql';` if the file is to run again.

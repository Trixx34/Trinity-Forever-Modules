-- Rollback for sql/custom/hotfixes/2026_11_12_39_hotfixes_classic_naxxramas_face_of_death_hungering_cold.sql. Not run by the updater, run by hand on the
-- hotfixes database (restart the worldserver after it; clients that cached the items keep them until their hotfix cache is cleared).
DELETE FROM `hotfix_data` WHERE (`TableHash` = 2442913102 AND `RecordId` IN (23043,23577))
   OR ((`TableHash` = 1073915313 OR `TableHash` = 13330255) AND `RecordId` = 9910101)
   OR `Id` IN (2088000,2088001,2088002,2088003);
DELETE FROM `item_x_item_effect` WHERE `ID` = 9910101;
DELETE FROM `item_effect` WHERE `ID` = 9910101;
DELETE FROM `item_sparse` WHERE `ID` IN (23043,23577);
-- Afterwards `DELETE FROM updates WHERE name = '2026_11_12_39_hotfixes_classic_naxxramas_face_of_death_hungering_cold.sql';` if the file is to run again.

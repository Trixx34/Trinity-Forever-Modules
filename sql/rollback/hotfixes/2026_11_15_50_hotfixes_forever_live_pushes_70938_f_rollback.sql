-- Rollback for sql/custom/hotfixes/2026_11_15_50_hotfixes_forever_live_pushes_70938_f.sql. NOT run by the updater: run it by hand on the hotfixes database (forever_*_hotfixes); restart the worldserver afterwards.
-- Clients that cached the changed records keep them until their hotfix cache is cleared.
-- Generated from the file's own statements: the active DELETEs remove the rows the file wrote (same keys the file deletes before it inserts).
-- Rows the file itself DELETED or UPDATED cannot be restored exactly without a before-image. Take it BEFORE the file is applied (STEP 0), then use STEP 2.
--
-- STEP 0 (before applying the file, optional but needed for an exact undo of deleted/updated rows):
--   CREATE TABLE `bak_eb4079f6_hotfix_data` AS SELECT * FROM `hotfix_data` WHERE (`Id` BETWEEN 2093000 AND 2093999) OR ((`TableHash`,`RecordId`) IN ((4030871717,1273551),(3322146344,815985),(3322146344,815988),(3322146344,815990),(3322146344,815992),(3322146344,815995),(3322146344,816001),(3322146344,816004),(3322146344,816010),(3322146344,816012),(3322146344,816015),(3322146344,816019),(3322146344,816045),(3322146344,816046),(3322146344,816048),(3322146344,816050),(3322146344,816055),(3322146344,816059),(3322146344,816060),(3322146344,816065),(3322146344,816066),(3322146344,816072),(3322146344,816073),(3322146344,816076),(3322146344,816077),(3322146344,816079),(3322146344,816084),(3322146344,816085),(3322146344,816086),(3322146344,816088),(3322146344,816089),(3322146344,831023),(3322146344,838852)));
--   CREATE TABLE `bak_eb4079f6_spell_effect` AS SELECT * FROM `spell_effect` WHERE (`ID` IN (1273551));
--   CREATE TABLE `bak_eb4079f6_spell_misc` AS SELECT * FROM `spell_misc` WHERE (`ID` IN (815985,815988,815990,815992,815995,816004,816010,816012,816015,816019,816045,816046,816048,816055,816066,816073,816076,816077,816079,816084,816085,816086,816088,816089,831023,838852));
--
-- STEP 1 (the undo that needs no before-image):
DELETE FROM `hotfix_data` WHERE (`Id` BETWEEN 2093000 AND 2093999) OR ((`TableHash`,`RecordId`) IN ((4030871717,1273551),(3322146344,815985),(3322146344,815988),(3322146344,815990),(3322146344,815992),(3322146344,815995),(3322146344,816001),(3322146344,816004),(3322146344,816010),(3322146344,816012),(3322146344,816015),(3322146344,816019),(3322146344,816045),(3322146344,816046),(3322146344,816048),(3322146344,816050),(3322146344,816055),(3322146344,816059),(3322146344,816060),(3322146344,816065),(3322146344,816066),(3322146344,816072),(3322146344,816073),(3322146344,816076),(3322146344,816077),(3322146344,816079),(3322146344,816084),(3322146344,816085),(3322146344,816086),(3322146344,816088),(3322146344,816089),(3322146344,831023),(3322146344,838852)));   -- the file's own delete condition: removes what it wrote
DELETE FROM `spell_effect` WHERE (`ID` IN (1273551));   -- the file's own delete condition: removes what it wrote
DELETE FROM `spell_misc` WHERE (`ID` IN (815985,815988,815990,815992,815995,816004,816010,816012,816015,816019,816045,816046,816048,816055,816066,816073,816076,816077,816079,816084,816085,816086,816088,816089,831023,838852));   -- the file's own delete condition: removes what it wrote
--
-- STEP 2 (only if STEP 0 was done before applying: put the old rows back, then drop the backup):
--   REPLACE INTO `hotfix_data` SELECT * FROM `bak_eb4079f6_hotfix_data`;  DROP TABLE `bak_eb4079f6_hotfix_data`;
--   REPLACE INTO `spell_effect` SELECT * FROM `bak_eb4079f6_spell_effect`;  DROP TABLE `bak_eb4079f6_spell_effect`;
--   REPLACE INTO `spell_misc` SELECT * FROM `bak_eb4079f6_spell_misc`;  DROP TABLE `bak_eb4079f6_spell_misc`;
-- Afterwards `DELETE FROM updates WHERE name = '2026_11_15_50_hotfixes_forever_live_pushes_70938_f.sql';` if the file is to run again.

-- Rollback for sql/custom/world/2026_11_14_14_world_forever_feral_charge_cat.sql. NOT run by the updater: run it by hand on the world database (forever_*_world); restart the worldserver afterwards.
-- Generated from the file's own statements: the active DELETEs remove the rows the file wrote (same keys the file deletes before it inserts).
-- Rows the file itself DELETED or UPDATED cannot be restored exactly without a before-image. Take it BEFORE the file is applied (STEP 0), then use STEP 2.
--
-- STEP 0 (before applying the file, optional but needed for an exact undo of deleted/updated rows):
--   CREATE TABLE `bak_d1042f77_spell_learn_spell` AS SELECT * FROM `spell_learn_spell` WHERE (`entry` = 1238122 AND `SpellID` IN (414925, 1238124));
--   CREATE TABLE `bak_d1042f77_spell_script_names` AS SELECT * FROM `spell_script_names` WHERE (`ScriptName` = 'spell_dru_classic_feral_charge_cat');
--
-- STEP 1 (the undo that needs no before-image):
DELETE FROM `spell_learn_spell` WHERE (`entry` = 1238122 AND `SpellID` IN (414925, 1238124));   -- the file's own delete condition: removes what it wrote
DELETE FROM `spell_script_names` WHERE (`ScriptName` = 'spell_dru_classic_feral_charge_cat');   -- the file's own delete condition: removes what it wrote
--
-- STEP 2 (only if STEP 0 was done before applying: put the old rows back, then drop the backup):
--   REPLACE INTO `spell_learn_spell` SELECT * FROM `bak_d1042f77_spell_learn_spell`;  DROP TABLE `bak_d1042f77_spell_learn_spell`;
--   REPLACE INTO `spell_script_names` SELECT * FROM `bak_d1042f77_spell_script_names`;  DROP TABLE `bak_d1042f77_spell_script_names`;
-- Afterwards `DELETE FROM updates WHERE name = '2026_11_14_14_world_forever_feral_charge_cat.sql';` if the file is to run again.

-- Rollback for sql/custom/world/2026_11_12_40_world_classic_naxxramas_face_of_death_hungering_cold.sql. Not run by the updater, run by hand on the world database.
-- Removes the two vanilla durability rows; the items then get the formula durability again.
DELETE FROM `classic_item_durability` WHERE `entry` IN (23043,23577);

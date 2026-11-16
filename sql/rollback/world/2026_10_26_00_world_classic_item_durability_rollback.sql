-- Rollback for sql/custom/world/2026_10_26_00_world_classic_item_durability.sql (and, through it, 2026_11_12_40). NOT run by the updater: run it by hand
-- on the world database. With the table gone the core of this branch (ObjectMgr::LoadClassicItemDurability) gets a failed query at start,
-- so roll back the code first or the worldserver start prints a DB error for the missing table. Run 2026_11_12_40 rollback first (same table).
-- Afterwards `DELETE FROM updates WHERE name IN ('2026_10_26_00_world_classic_item_durability.sql', '2026_11_12_40_world_classic_naxxramas_face_of_death_hungering_cold.sql');` if the files are to run again.
DROP TABLE IF EXISTS `classic_item_durability`;

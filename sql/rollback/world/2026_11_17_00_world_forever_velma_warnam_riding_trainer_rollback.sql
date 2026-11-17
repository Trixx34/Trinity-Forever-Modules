-- Rollback for sql/custom/world/2026_11_17_00_world_forever_velma_warnam_riding_trainer.sql. NOT run by the updater: run it by hand on the world database (forever_*_world); restart the worldserver afterwards.
-- Puts Velma Warnam (4773) back on trainer 195 (the row before the file: TrainerID 195, MenuID 4015, OptionID 0). Idempotent.
UPDATE `creature_trainer` SET `TrainerID` = 195 WHERE `CreatureID` = 4773 AND `TrainerID` = 1004773;
-- Afterwards `DELETE FROM updates WHERE name = '2026_11_17_00_world_forever_velma_warnam_riding_trainer.sql';` if the file is to run again.

-- Velma Warnam (Undercity, creature 4773) was the one riding trainer on trainer 195, which teaches 33389 and 33392. The other six riding trainers
-- (4732, 4752, 4753, 4772, 7953, 7954, 3690) use the Forever trainers 10xxxxxx that teach 33388 (level 40) and 33391 (level 60, needs 33388).
-- Trainer 1004773 holds the same two spells and no creature pointed at it. Trainer 195 is used by Velma alone. Idempotent: only a row still on 195 changes.
UPDATE `creature_trainer` SET `TrainerID` = 1004773 WHERE `CreatureID` = 4773 AND `TrainerID` = 195;

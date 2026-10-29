-- Classic 1.60: Call Pet (883) had no trainer row, so no hunter bot could learn it ("spell not known" every time it tried).
-- The hunter class trainers (every trainer that teaches Hunter's Mark, 1130) teach it at level 10. Cost and level are the classic
-- values, not sniffed: the official sniffed lists (sql/custom/world/2026_10_15_00_world_classic_class_trainers.sql) do not carry it.
-- Safe to run again.
DELETE FROM `trainer_spell` WHERE `SpellId` = 883;
INSERT INTO `trainer_spell` (`TrainerId`, `SpellId`, `MoneyCost`, `ReqSkillLine`, `ReqSkillRank`, `ReqAbility1`, `ReqAbility2`, `ReqAbility3`, `ReqLevel`, `VerifiedBuild`)
SELECT DISTINCT `TrainerId`, 883, 10, 0, 0, 0, 0, 0, 10, 0 FROM `trainer_spell` WHERE `SpellId` = 1130;

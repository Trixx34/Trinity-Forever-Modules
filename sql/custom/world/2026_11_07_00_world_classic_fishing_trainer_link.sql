-- Prod check 2026-11-07: 2026_11_04_00_world_classic_profession_trainer_flags.sql dropped the wrong shaman link (trainer 10) from
-- menu 12046 option 2 ("Fishing") of 10296 Acride and 45563 Tinza Silvermug, which left the option without a trainer.
-- Trainer 100 is the fishing trainer (Apprentice Fishing 7733, Journeyman Fishing 7734, "I can teach you how to use a fishing pole").
-- Safe to run again.
-- rollback: DELETE FROM `creature_trainer` WHERE `CreatureID` IN (10296, 45563) AND `MenuID` = 12046 AND `OptionID` = 2;
DELETE FROM `creature_trainer` WHERE `CreatureID` IN (10296, 45563) AND `MenuID` = 12046 AND `OptionID` = 2;
INSERT INTO `creature_trainer` (`CreatureID`, `TrainerID`, `MenuID`, `OptionID`) VALUES
(10296, 100, 12046, 2),
(45563, 100, 12046, 2);

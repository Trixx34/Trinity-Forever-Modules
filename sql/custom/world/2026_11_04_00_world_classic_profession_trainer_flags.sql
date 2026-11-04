-- Prod log findings (2026-10-18 .. 2026-11-04): profession trainers that could not be talked to or trained from, and a stale shaman link.
-- Safe to run again.
-- rollback: UPDATE creature_template SET npcflag = 0 WHERE entry IN (92459,16742);
--           DELETE FROM creature_template_gossip WHERE CreatureID = 65043 AND MenuID = 12180;
--           INSERT INTO creature_trainer (CreatureID,TrainerID,MenuID,OptionID) VALUES (10296,10,12046,2),(45563,10,12046,2);

-- 92459 Dorothy Egan (Herbalism Trainer, trainer 133) and 16742 Kudrii (Apprentice Enchanter, trainer 62, menu 10365 "Train me."):
-- npcflag was 0, so the client showed no gossip/trainer interaction. 81 = gossip + trainer + profession trainer, the value of every other
-- single-profession trainer (e.g. 812 Alma Jainrose, 1317 Lucan Cordell).
UPDATE `creature_template` SET `npcflag` = `npcflag` | 81 WHERE `entry` IN (92459, 16742);

-- 65043 Elder Oakpaw (Profession Trainer): the same trainer rows as 48619 Therisa Sallow (menu 12180) but no creature_template_gossip
-- row, so the profession menu was never shown. npcflag 1 (gossip) is right: the trainer options of menu 12180 are gossip options.
DELETE FROM `creature_template_gossip` WHERE `CreatureID` = 65043;
INSERT INTO `creature_template_gossip` (`CreatureID`, `MenuID`, `VerifiedBuild`) VALUES (65043, 12180, 0);

-- 10296 Acride and 45563 Tinza Silvermug (innkeeper): menu 12046 option 2 is "Fishing" but the sniff import linked it to trainer 10
-- ("Hello, shaman!", class trainer, 168 spells). Drop the wrong link.
DELETE FROM `creature_trainer` WHERE `CreatureID` IN (10296, 45563) AND `TrainerID` = 10 AND `MenuID` = 12046 AND `OptionID` = 2;

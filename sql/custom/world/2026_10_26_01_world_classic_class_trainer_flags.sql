-- Classic 1.60: 12 class trainers had a class trainer list (creature_trainer) but no trainer flags (npcflag 16 Trainer, 32 Trainer
-- class), so the client showed no train option. Their trainer_class was 0, so the class filter did not match either. Set the flags
-- the other class trainers use (51 = gossip, questgiver, trainer, class trainer) and the class id (warrior 1, paladin 2, hunter 3,
-- rogue 4, mage 8, shaman 7, druid 11). Safe to run again: `| 48` and the class id are the same on a second run.
UPDATE `creature_template` SET `npcflag` = `npcflag` | 48, `trainer_class` = 1 WHERE `entry` IN (252377, 254088);  -- Seena Skybreaker, Corsan Earthrazer (Warrior)
UPDATE `creature_template` SET `npcflag` = `npcflag` | 48, `trainer_class` = 4 WHERE `entry` IN (252379, 254087);  -- Eltheen Nightbreeze, Miriaan Mistblade (Rogue)
UPDATE `creature_template` SET `npcflag` = `npcflag` | 48, `trainer_class` = 8 WHERE `entry` IN (252373, 254086);  -- Anathamaas Aetherwind, Shenaan Spellwind (Mage)
UPDATE `creature_template` SET `npcflag` = `npcflag` | 48, `trainer_class` = 3 WHERE `entry` IN (248415, 254084);  -- Tordrin Sternblade, Elayaa Easewind (Hunter)
UPDATE `creature_template` SET `npcflag` = `npcflag` | 48, `trainer_class` = 7 WHERE `entry` IN (252382, 254082);  -- Sessaria Skystride, Aarnor Galestrike (Shaman)
UPDATE `creature_template` SET `npcflag` = `npcflag` | 48, `trainer_class` = 2 WHERE `entry` = 246389;                -- Hilda the Breaker (Paladin)
UPDATE `creature_template` SET `npcflag` = `npcflag` | 48, `trainer_class` = 11 WHERE `entry` = 252359;               -- Lotheluum Starbreeze (Druid)

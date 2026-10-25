-- Баг-репорты игроков 25.10.2026: №1080 (пункт «Шаман» у стражи Стальгорна ничего не делает) и №1085 (Райли Пельтскиннер:
-- ни обучение, ни покупка не выбираются).
-- Источники: страницы wowhead.com/forever и база Questie для Forever (проверено 25.10.2026), снифы (сборки 70245–70425).
--   1) Стража Стальгорна, подменю классов 2144: пункт «Шаман» (OptionID 7) есть в снифе (сборка 70425), но без действия,
--      что происходит после выбора, в дампе нет. Заведена метка на карте у шаман-тренера Стальгорна — Элдруна Тучегона
--      (258098, он же Eldrun Stormbreaker; Wowhead Forever: «Shaman Trainer», Ironforge, учит 167 заклинаний, нашёлся в
--      нашей базе на (-4642.4, -1086.5, 500.6)), номер метки 900001 взят вне диапазона базы (максимум 10445). Окно
--      беседы не меняется: ядро только показывает метку.
--   2) Элдрун стоял без флага и привязки тренера (npcflag 3, нет creature_trainer и меню). Ему поставлен флаг тренера
--      (16), привязан тот же список заклинаний (trainer 10) и меню 41915, что у Ingrid Dunwald (258113).
--      Квесты Элдруна (Wowhead: начинает 4, принимает 1) не добавлены — в базе их связей нет, цепочку требований
--      это не определяет.
--   3) Райли Пельтскиннер (247227, Junior Skinning Trainer, стоит в Нортшире — подтверждено Wowhead и Questie): меню
--      39905 с «Train me.» и «I would like to buy from you.» есть, торговые строки есть (4), а тренера нет. Wowhead:
--      учит ровно одному — Skinning (Apprentice). В снифах настоящий тренер снятия шкур (trainer 113) отдаёт для
--      первой ступени заклинание 8615 за 10 медных, оно и взято. Номер тренера 1247227 в стиле базы (1000000 + entry).
-- Не выяснено: почему не открывалась «покупка» у Райли (товары в таблице есть, флаги и ID пунктов в порядке) —
-- возможно, пункт «купить» молчал из-за отсутствия привязки тренера в том же меню; проверить в игре после рестарта.
-- Можно запускать повторно.
-- (drsnuggles8 fork: Riley Peltskinner already gets the sniffed junior skinning list 1224 from
-- 2026_10_26_05_world_classic_trainers_without_trainer_list.sql, so the single-spell trainer of the original commit is left out.)

INSERT IGNORE INTO `points_of_interest` (`ID`, `PositionX`, `PositionY`, `PositionZ`, `Icon`, `Flags`, `Importance`, `Name`, `WMOGroupID`, `VerifiedBuild`) VALUES
(900001, -4642.4, -1086.5, 500.6, 6, 99, 0, 'Ironforge Shaman Trainer', 0, 0);

UPDATE `gossip_menu_option` SET `ActionPoiID` = 900001 WHERE `MenuID` = 2144 AND `OptionID` = 7 AND `ActionPoiID` = 0;

UPDATE `creature_template` SET `npcflag` = (`npcflag` | 16) WHERE `entry` = 258098;
DELETE FROM `creature_template_gossip` WHERE `CreatureID` = 258098;
INSERT INTO `creature_template_gossip` (`CreatureID`, `MenuID`, `VerifiedBuild`) VALUES (258098, 41915, 0);
DELETE FROM `creature_trainer` WHERE `CreatureID` = 258098;
INSERT INTO `creature_trainer` (`CreatureID`, `TrainerID`, `MenuID`, `OptionID`) VALUES (258098, 10, 41915, 0);

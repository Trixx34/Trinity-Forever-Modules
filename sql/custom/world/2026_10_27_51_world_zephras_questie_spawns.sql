-- Zephras Isle NPCs that quests need and the world DB never spawned (contrib/launch-sim/questie: "giver/ender/kill target
-- not spawned"): Elegael Thornpaw (gives or takes 8 quests, 94484-94491), Peacekeeper Vaaniel (93926 The Western Watch,
-- 93927 A Last Request) and Ferauu the Bludgeon (kill target of 92693 Standing Our Ground). Positions: the Forever
-- QuestieDB's map percent, converted with a fit over 145 Zephras NPCs spawned in both (median error 0.5 y), the height
-- snapped to the navmesh (mmap_probe; Elegael on the ground, reachable, not on the platform 90 y above). Safe to run again.
DELETE FROM `creature` WHERE `guid` BETWEEN 26399001 AND 26399003;
INSERT INTO `creature` (`guid`, `id`, `map`, `spawnDifficulties`, `position_x`, `position_y`, `position_z`, `orientation`, `spawntimesecs`) VALUES
(26399001, 257944, 2991, '0', 3501.9, 799.5, 799.73, 0, 120),  -- Elegael Thornpaw
(26399002, 252155, 2991, '0', 2656.4, 1875.4, 772.95, 0, 120),  -- Peacekeeper Vaaniel
(26399003, 252863, 2991, '0', 2055.5, 1646.3, 661.39, 0, 300);  -- Ferauu the Bludgeon

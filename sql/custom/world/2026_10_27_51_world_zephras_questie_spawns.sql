-- Zephras Isle NPC that a quest needs and the world DB never spawned (contrib/launch-sim/questie: "kill target not
-- spawned"): Ferauu the Bludgeon (kill target of 92693 Standing Our Ground). Position: the Forever QuestieDB's map percent,
-- converted with a fit over 145 Zephras NPCs spawned in both (median error 0.5 y), the height snapped to the navmesh
-- (mmap_probe). Elegael Thornpaw and Peacekeeper Vaaniel were spawned here too until upstream placed them at their sniffed
-- quest POI points (2026_10_24_61_world_forever_missing_quest_givers.sql); their rows 26399001-26399002 go. Safe to run again.
DELETE FROM `creature` WHERE `guid` BETWEEN 26399001 AND 26399003;
INSERT INTO `creature` (`guid`, `id`, `map`, `spawnDifficulties`, `position_x`, `position_y`, `position_z`, `orientation`, `spawntimesecs`) VALUES
(26399003, 252863, 2991, '0', 2055.5, 1646.3, 661.39, 0, 300);  -- Ferauu the Bludgeon

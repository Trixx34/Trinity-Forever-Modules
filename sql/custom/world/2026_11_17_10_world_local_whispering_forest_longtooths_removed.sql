-- Whispering Forest (Tirisfal, level 14 area): remove the 15 misplaced level 47-48 Rabid Longtooth spawns (entry 5288, guids 21200001-21200015,
-- 2,938 hp; Discord report 2026-11-12). Trimmed from Karrz's 2026_11_16_70 (SIN's 0.1.35 "Whispering Forest spawn fixes"): only this part is
-- taken. His Horror trimming and CHECK guards are NOT part of this file (they need the 0.1.34 backlog data), so the file cannot stop the updater.
-- Plain DELETEs, safe to run twice. Dependent rows go first (joined to the creature row so a guid reused by another entry is never touched),
-- the creature rows last. Dependent tables are empty for these guids on the sim today; the statements keep the file safe on other realms.
-- Out of scope: the four level 8-9 Rabid Forest Wolves (27773000-27773003) and the Whispering Horrors.
DELETE a FROM creature_addon a JOIN creature c ON c.guid = a.guid
WHERE c.guid BETWEEN 21200001 AND 21200015 AND c.id = 5288 AND c.map = 0;
DELETE f FROM creature_formations f JOIN creature c ON c.guid = f.leaderGUID OR c.guid = f.memberGUID
WHERE c.guid BETWEEN 21200001 AND 21200015 AND c.id = 5288 AND c.map = 0;
DELETE m FROM creature_movement_override m JOIN creature c ON c.guid = m.SpawnId
WHERE c.guid BETWEEN 21200001 AND 21200015 AND c.id = 5288 AND c.map = 0;
DELETE s FROM creature_static_flags_override s JOIN creature c ON c.guid = s.SpawnId
WHERE c.guid BETWEEN 21200001 AND 21200015 AND c.id = 5288 AND c.map = 0;
DELETE e FROM game_event_creature e JOIN creature c ON c.guid = e.guid
WHERE c.guid BETWEEN 21200001 AND 21200015 AND c.id = 5288 AND c.map = 0;
DELETE e FROM game_event_model_equip e JOIN creature c ON c.guid = e.guid
WHERE c.guid BETWEEN 21200001 AND 21200015 AND c.id = 5288 AND c.map = 0;
DELETE e FROM game_event_npc_vendor e JOIN creature c ON c.guid = e.guid
WHERE c.guid BETWEEN 21200001 AND 21200015 AND c.id = 5288 AND c.map = 0;
DELETE e FROM game_event_npcflag e JOIN creature c ON c.guid = e.guid
WHERE c.guid BETWEEN 21200001 AND 21200015 AND c.id = 5288 AND c.map = 0;
DELETE l FROM linked_respawn l JOIN creature c ON c.guid = l.guid OR c.guid = l.linkedGuid
WHERE c.guid BETWEEN 21200001 AND 21200015 AND c.id = 5288 AND c.map = 0;
DELETE p FROM pool_members p JOIN creature c ON c.guid = p.spawnId
WHERE p.type = 0 AND c.guid BETWEEN 21200001 AND 21200015 AND c.id = 5288 AND c.map = 0;
DELETE s FROM smart_scripts s JOIN creature c ON c.guid = -CAST(s.entryorguid AS SIGNED)
WHERE s.source_type = 0 AND s.entryorguid < 0 AND c.guid BETWEEN 21200001 AND 21200015 AND c.id = 5288 AND c.map = 0;
DELETE g FROM spawn_group g JOIN creature c ON c.guid = g.spawnId
WHERE g.spawnType = 0 AND c.guid BETWEEN 21200001 AND 21200015 AND c.id = 5288 AND c.map = 0;
DELETE t FROM spawn_tracking t JOIN creature c ON c.guid = t.SpawnId
WHERE t.SpawnType = 0 AND c.guid BETWEEN 21200001 AND 21200015 AND c.id = 5288 AND c.map = 0;
DELETE t FROM spawn_tracking_state t JOIN creature c ON c.guid = t.SpawnId
WHERE t.SpawnType = 0 AND c.guid BETWEEN 21200001 AND 21200015 AND c.id = 5288 AND c.map = 0;
DELETE v FROM vehicle_accessory v JOIN creature c ON c.guid = v.guid
WHERE c.guid BETWEEN 21200001 AND 21200015 AND c.id = 5288 AND c.map = 0;
DELETE FROM creature WHERE guid BETWEEN 21200001 AND 21200015 AND id = 5288 AND map = 0;

-- The Fate of a Loved One (94897): Resaan's "Retrieve the heirloom" option had no
-- menu transition or script action. Show it only while the quest is active and
-- award the sniffed heirloom (266434) when selected.
-- Aid for the Refugees (94896): the sniffed chest template accidentally used Lock.db2
-- record 4088 (Dreadblade, skill requirement 42), preventing ordinary players from opening it.
UPDATE `gameobject_template` SET `Data0` = 0 WHERE `entry` = 623295 AND `type` = 3 AND `Data0` = 4088;

DELETE FROM `conditions`
WHERE `SourceTypeOrReferenceId` = 15 AND `SourceGroup` = 42472 AND `SourceEntry` = 0
  AND `SourceId` = 0 AND `ConditionTypeOrReference` = 47 AND `ConditionValue1` = 94897;
INSERT INTO `conditions`
  (`SourceTypeOrReferenceId`,`SourceGroup`,`SourceEntry`,`SourceId`,`ElseGroup`,
   `ConditionTypeOrReference`,`ConditionTarget`,`ConditionValue1`,`ConditionValue2`,
   `ConditionValue3`,`NegativeCondition`,`ErrorType`,`ErrorTextId`,`ScriptName`,`Comment`)
VALUES
  (15,42472,0,0,0,47,0,94897,8,0,0,0,0,'','The Fate of a Loved One: heirloom option requires the quest to be active'),
  (15,42472,0,0,0,2,0,266434,1,0,1,0,0,'','The Fate of a Loved One: hide the option after the heirloom is received');

UPDATE `creature_template` SET `AIName` = 'SmartAI' WHERE `entry` = 259013;
DELETE FROM `smart_scripts` WHERE `entryorguid` = 259013 AND `source_type` = 0 AND `id` IN (0, 1);
INSERT INTO `smart_scripts`
  (`entryorguid`,`source_type`,`id`,`link`,`event_type`,`event_phase_mask`,`event_chance`,
   `event_flags`,`event_param1`,`event_param2`,`event_param3`,`event_param4`,`action_type`,
   `action_param1`,`action_param2`,`target_type`,`comment`)
VALUES
  (259013,0,0,0,62,0,100,0,42472,0,0,0,56,266434,1,7,
   'Resaan Nimbuswalker - Retrieve the heirloom - give heirloom to the player');

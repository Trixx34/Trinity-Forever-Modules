-- .battletag [name] (cs_battlenet_account.cpp): every player may show or pick their account's BattleTag; the number is drawn.
-- 1005 = Command: battletag (1002 is .bug on this core, 1003 gm combatlog, 1004 deathrecap) (RBAC_PERM_COMMAND_BATTLETAG), 195 = Role: Player. Safe to run again.
INSERT IGNORE INTO `rbac_permissions` (`id`, `name`) VALUES (1005, 'Command: battletag');
INSERT IGNORE INTO `rbac_linked_permissions` (`id`, `linkedId`) VALUES (195, 1005);

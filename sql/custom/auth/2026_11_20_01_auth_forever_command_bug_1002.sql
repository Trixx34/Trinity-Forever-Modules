-- .bug <text> (cs_misc.cpp, from Karrz1 0.1.38): every player may send a bug report that carries their spot, target and quest log.
-- Here the permission is 1002 = Command: bug (RBAC_PERM_COMMAND_BUG); Karrz's own file (2026_11_17_01_auth_command_bug.sql) uses
-- 1001, which on this core is COMMAND_MODULE (2026_10_12_00), and linking 1001 to the player role would give every player the
-- module commands. That file is not taken. 195 = Role: Player. Safe to run again.
INSERT IGNORE INTO `rbac_permissions` (`id`, `name`) VALUES (1002, 'Command: bug');
INSERT IGNORE INTO `rbac_linked_permissions` (`id`, `linkedId`) VALUES (195, 1002);

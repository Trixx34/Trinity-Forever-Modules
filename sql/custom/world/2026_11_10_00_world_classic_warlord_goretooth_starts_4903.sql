-- Warlord's Command (4903) had a quest ender (Warlord Goretooth 9077, who also ends 4081, 4082 and 4132) but no starter row, so nobody could take it
-- (found by the bots' quest generator, 2026-11-10: "no creature giver spawn"). Goretooth is its giver; he already starts 4132 and 4941.
-- Safe to run again.
DELETE FROM `creature_queststarter` WHERE `id` = 9077 AND `quest` = 4903;
INSERT INTO `creature_queststarter` (`id`, `quest`, `VerifiedBuild`) VALUES
(9077, 4903, 0);

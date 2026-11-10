-- Nightfall (18094, script classic_spell_warl_nightfall): the aura had no spell_proc row, so it was offered every damage spell the warlock cast
-- (the client spell data gives it no SpellClassMask). Only Corruption, Drain Soul, Drain Life and Siphon Life damage may give Shadow Trance, as the tooltip says.
-- Warlock spell family 5, SpellClassMask from the client's SpellClassOptions: Corruption 0x2, Drain Life 0x8, Drain Soul 0x4000 (word 0), Siphon Life 0x1 (word 1).
-- ProcFlags, hit and phase masks stay 0 (the spell's own); the chance roll stays in the script.
-- Safe to run again.
DELETE FROM `spell_proc` WHERE `SpellId` = 18094;
INSERT INTO `spell_proc` (`SpellId`, `SchoolMask`, `SpellFamilyName`, `SpellFamilyMask0`, `SpellFamilyMask1`, `SpellFamilyMask2`, `SpellFamilyMask3`, `ProcFlags`, `ProcFlags2`, `SpellTypeMask`, `SpellPhaseMask`, `HitMask`, `AttributesMask`, `DisableEffectsMask`, `ProcsPerMinute`, `Chance`, `Cooldown`, `Charges`) VALUES
(18094, 0, 5, 0x400A, 0x1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

-- Warrior trainers Seena Skybreaker (252377), Corsan Earthrazer (254088) and Blademaster Ren (251964) share gossip menu 41126, which
-- had no trainer option, so the client never showed "I'd like training!" even with the trainer flags set. Add the option the other
-- trainer menus use (OptionNpc 3 = trainer, the same row as menu 41123 for Quel'ana Quickgale).
INSERT IGNORE INTO `gossip_menu_option` (`MenuID`, `GossipOptionID`, `OptionID`, `OptionNpc`, `OptionText`, `OptionBroadcastTextID`, `Language`, `Flags`, `ActionMenuID`, `ActionPoiID`, `GossipNpcOptionID`, `BoxCoded`, `BoxMoney`, `BoxText`, `BoxBroadcastTextID`, `SpellID`, `OverrideIconID`, `VerifiedBuild`) VALUES
(41126, 136809, 0, 3, 'I\'d like training!', 0, 0, 0, 0, 0, NULL, 0, 0, NULL, 0, NULL, NULL, 70124);

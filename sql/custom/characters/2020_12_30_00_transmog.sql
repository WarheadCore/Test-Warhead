-- Add column
ALTER TABLE `item_instance` ADD COLUMN `transmog` INT UNSIGNED NOT NULL DEFAULT '0' AFTER `text`;

-- ----------------------------
-- Table structure for custom_transmogrification_sets
-- ----------------------------
DROP TABLE IF EXISTS `custom_transmogrification_sets`;
CREATE TABLE `custom_transmogrification_sets`  (
  `Owner` int(10) UNSIGNED NOT NULL COMMENT 'Player guidlow',
  `PresetID` tinyint(3) UNSIGNED NOT NULL COMMENT 'Preset identifier',
  `SetName` text CHARACTER SET utf8 COLLATE utf8_general_ci NULL DEFAULT NULL COMMENT 'SetName',
  `SetData` text CHARACTER SET utf8 COLLATE utf8_general_ci NULL DEFAULT NULL COMMENT 'Slot1 Entry1 Slot2 Entry2',
  PRIMARY KEY (`Owner`, `PresetID`) USING BTREE
) ENGINE = InnoDB CHARACTER SET = utf8 COLLATE = utf8_general_ci COMMENT = '6_1' ROW_FORMAT = Dynamic;

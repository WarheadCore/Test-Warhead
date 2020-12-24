-- To IP
UPDATE `warhead_string` SET `content_default` = '│Banned: (Type: IP, Left %s, Time: %s, By: %s, Reason: %s)' WHERE `entry` = 549;

-- Add for acc and char
DELETE FROM `warhead_string` WHERE `entry` IN (788, 789);
INSERT INTO `warhead_string`(`entry`, `content_default`) VALUES (788, '│Banned: (Type: Account, Left %s, Time: %s, By: %s, Reason: %s)');
INSERT INTO `warhead_string`(`entry`, `content_default`) VALUES (789, '│Banned: (Type: Character, Left %s, Time: %s, By: %s, Reason: %s)');

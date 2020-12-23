-- Replace LANG_PINFO_MUTED
UPDATE `warhead_string` SET `content_default` = '├─ Muted: (Reason: %s, Time: %s, Left %s, By: %s)' WHERE `entry` = 550;
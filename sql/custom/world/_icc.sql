/* Nerub'ar Broodkeeper SmartAI
UPDATE `creature_template` SET `AIName` = 'SmartAI' WHERE `entry` = 36725;

DELETE FROM `smart_scripts` WHERE (`source_type` = 0 AND `entryorguid` = 36725);
INSERT INTO `smart_scripts` (`entryorguid`, `source_type`, `id`, `link`, `event_type`, `event_phase_mask`, `event_chance`, `event_flags`, `event_param1`, `event_param2`, `event_param3`, `event_param4`, `event_param5`, `action_type`, `action_param1`, `action_param2`, `action_param3`, `action_param4`, `action_param5`, `action_param6`, `target_type`, `target_param1`, `target_param2`, `target_param3`, `target_param4`, `target_x`, `target_y`, `target_z`, `target_o`, `comment`) VALUES
(36725, 0, 1, 0, 10, 0, 100, 30, 0, 60, 1000, 1000, 1, 38, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 'Nerub\'ar Broodkeeper - Within 0-60 Range Out of Combat LoS - Set In Combat With Zone'),
(36725, 0, 2, 0, 0, 0, 100, 30, 5000, 7000, 5000, 9000, 0, 11, 70965, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 'Nerub\'ar Broodkeeper - In Combat - Cast \'Crypt Scarabs\''),
(36725, 0, 3, 0, 0, 0, 100, 30, 5000, 7000, 5000, 9000, 0, 11, 70980, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 'Nerub\'ar Broodkeeper - In Combat - Cast \'Web Wrap\''),
(36725, 0, 4, 0, 14, 0, 100, 30, 5000, 30, 8000, 13000, 0, 11, 36725, 0, 0, 0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0, 'Nerub\'ar Broodkeeper - Friendly At 5000 Health - Cast \'Spirit Particles (red)\''),
(36725, 0, 5, 0, 4, 0, 100, 30, 0, 0, 0, 0, 0, 11, 69887, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 'Nerub\'ar Broodkeeper - On aggro - Cast Web Beam'),
(36725, 0, 6, 0, 0, 0, 100, 543, 4000, 4000, 0, 0, 0, 91, 2, 3, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 'Nerub\'ar Broodkeeper  - In combat no repeat - Remove Flag Hover');
*/

UPDATE `creature_template` SET `AIName` = '', `ScriptName` = 'npc_icc_nerubar_broodkeeper' WHERE `entry` = 36725;
DELETE FROM `smart_scripts` WHERE `source_type` = 0 AND `entryorguid` = 36725;

/*
 * Spells
 */

/*DELETE FROM `spell_script_names` WHERE spell_id = 70980;
INSERT INTO `spell_script_names`(`spell_id`, `ScriptName`) VALUES (70980, 'spell_icc_web_wrap');*/

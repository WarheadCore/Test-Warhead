/*
 * This file is part of the WarheadCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _CROSSFACTION_BG_H_
#define _CROSSFACTION_BG_H_

#include "Common.h"

class BattlegroundQueue;
class Battleground;

enum BattlegroundBracketId : uint8;

class CFBGQueue
{
public:
    static CFBGQueue* instance();

    bool CheckMixedMatch(BattlegroundQueue* queue, Battleground* bg_template, BattlegroundBracketId bracket_id, uint32 minPlayers, uint32 maxPlayers);
    bool MixPlayersToBG(BattlegroundQueue* queue, Battleground* bg, BattlegroundBracketId bracket_id);
    bool CFBGGroupInserter(BattlegroundQueue* queue, Battleground* bg_template, BattlegroundBracketId bracket_id, uint32 minPlayers, uint32 maxPlayers, uint32 minplayers);
};

#define sCFBGQueue CFBGQueue::instance()

#endif // _CROSSFACTION_BG_H_

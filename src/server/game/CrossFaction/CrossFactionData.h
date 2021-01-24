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

#ifndef _CROSS_FACTION_DATA_H_
#define _CROSS_FACTION_DATA_H_

#include "Common.h"
#include "Player.h"

class Player;

class WH_GAME_API CrossFactionData
{
public:
    CrossFactionData(Player* player);

    bool NativeTeam() { return _player->GetTeam() == GetOriginalTeam(); }
    uint8 GetFakeRace() const { return _fakeRace; }
    uint8 GetOriginalRace() const { return _originalRace; }
    uint32 GetOriginalFaction() const { return _originalFaction; }
    uint32 GetFakeFaction() const { return _fakeFaction; }
    uint32 GetOriginalTeam() const { return _originalTeam; }
    void SetCrossFactionData();
    void ReplaceRacials();
    void ReplaceItems();
    void InitializeData();
    void SetRaceDisplayID();

private:
    Player* _player;
    uint8 _fakeRace;
    uint8 _originalRace;
    uint32 _fakeFaction;
    uint32 _originalFaction;
    uint32 _fakeTeam;
    uint32 _originalTeam;
};

#endif // _CROSS_FACTION_DATA_H_

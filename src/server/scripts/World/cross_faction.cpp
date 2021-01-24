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

#include "ScriptMgr.h"
#include "Player.h"
#include "CrossFactionData.h"

class CrossFactionData_Player : public PlayerScript
{
public:
    CrossFactionData_Player() : PlayerScript("CrossFactionData_Player") { }

    void OnLogin(Player* player, bool /*firstLogin*/)
    {
        auto CFData = player->GetCrossFactionData();
        if (!CFData)
            return;

        if (!CFData->NativeTeam())
        {
            uint8 originalRace = CFData->GetOriginalRace();

            player->SetRace(originalRace);
            player->SetFactionForRace(originalRace);
            CFData->SetRaceDisplayID();
        }
    }
};

void AddSC_CrossFacrionData()
{
    new CrossFactionData_Player();
}

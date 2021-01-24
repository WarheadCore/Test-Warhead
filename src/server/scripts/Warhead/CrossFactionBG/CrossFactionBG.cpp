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

#include "CrossFactionBG.h"
#include "Log.h"
#include "GameConfig.h"
#include "BattlegroundMgr.h"

class GroupList : public std::list<GroupQueueInfo*>
{
public:
    void AddGroups(std::list<GroupQueueInfo*> list)
    {
        insert(end(), list.begin(), list.end());
    }

    void Sort()
    {
        sort([](GroupQueueInfo* a, GroupQueueInfo* b)
        {
            return a->JoinTime < b->JoinTime;
        });
    }
};

CFBGQueue* CFBGQueue::instance()
{
    static CFBGQueue instance;
    return &instance;
}

bool CFBGQueue::CheckMixedMatch(BattlegroundQueue* queue, Battleground* bg_template, BattlegroundBracketId bracket_id, uint32 minPlayers, uint32 maxPlayers)
{
    return CFBGGroupInserter(queue, bg_template, bracket_id, maxPlayers, maxPlayers, minPlayers);
}

bool CFBGQueue::MixPlayersToBG(BattlegroundQueue* queue, Battleground* bg, BattlegroundBracketId bracket_id)
{
    return CFBGGroupInserter(queue, bg, bracket_id, bg->GetFreeSlotsForTeam(ALLIANCE), bg->GetFreeSlotsForTeam(HORDE), 0);
}

bool CFBGQueue::CFBGGroupInserter(BattlegroundQueue* queue, Battleground* bg, BattlegroundBracketId bracket_id, uint32 AllyFree, uint32 HordeFree, uint32 MinPlayers)
{
    if (!CONF_GET_BOOL("CFBG.Enable") || !bg->isBattleground())
        return false;

    // MinPlayers is only 0 when we're filling an existing BG.
    bool Filling = MinPlayers == 0;

    uint32 MaxAlly = AllyFree;
    uint32 MaxHorde = HordeFree;

    queue->m_SelectionPools[TEAM_ALLIANCE].Init();
    queue->m_SelectionPools[TEAM_HORDE].Init();

    // If players on different factions queue at the same second it'll be random who gets added first
    bool AllyFirst = urand(0, 1);
    auto Groups = GroupList();

    Groups.AddGroups(queue->m_QueuedGroups[bracket_id][AllyFirst ? BG_QUEUE_NORMAL_ALLIANCE : BG_QUEUE_NORMAL_HORDE]);
    Groups.AddGroups(queue->m_QueuedGroups[bracket_id][AllyFirst ? BG_QUEUE_NORMAL_HORDE : BG_QUEUE_NORMAL_ALLIANCE]);
    Groups.Sort();

    bool startable = false;

    for (auto& ginfo : Groups)
    {
        if (ginfo->IsInvitedToBGInstanceGUID)
            continue;

        bool AddAsAlly = AllyFree == HordeFree ? ginfo->OriginalTeam == ALLIANCE : AllyFree > HordeFree;
        AddAsAlly = !AddAsAlly;

        ginfo->Team = AddAsAlly ? ALLIANCE : HORDE;

        if (queue->m_SelectionPools[AddAsAlly ? TEAM_ALLIANCE : TEAM_HORDE].AddGroup(ginfo, AddAsAlly ? MaxAlly : MaxHorde))
            AddAsAlly ? AllyFree -= ginfo->Players.size() : HordeFree -= ginfo->Players.size();
        else if (!Filling)
            break;

        // Return when we're ready to start a BG, if we're in startup process
        if (queue->m_SelectionPools[TEAM_ALLIANCE].GetPlayerCount() >= MinPlayers &&
            queue->m_SelectionPools[TEAM_HORDE].GetPlayerCount() >= MinPlayers &&
            !Filling)
            startable = true;
    }

    if (startable)
        return true;

    // If we're in BG testing one player is enough
    if (sBattlegroundMgr->isTesting() && queue->m_SelectionPools[TEAM_ALLIANCE].GetPlayerCount() + queue->m_SelectionPools[TEAM_HORDE].GetPlayerCount() > 0)
        return true;

    // Filling always returns true
    if (Filling)
        return true;

    // Return false when we didn't manage to fill the BattleGround in Filling "mode".
    // reset selectionpool for further attempts
    queue->m_SelectionPools[TEAM_ALLIANCE].Init();
    queue->m_SelectionPools[TEAM_HORDE].Init();

    return false;
}

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
#include "CrossFactionBG.h"
#include "Battleground.h"
#include "DBCEnums.h"
#include "BattlegroundQueue.h"
#include "GameConfig.h"
#include "CharacterCache.h"
#include "Chat.h"
#include "WorldSession.h"

class CrossFactionBG_BG : public BGScript
{
public:
    CrossFactionBG_BG() : BGScript("CrossFactionBG_BG") { }

    void OnBattlegroundBeforeAddPlayer(Battleground* bg, Player* player) override
    {
        sCharacterCache->UpdateCharacterData(player->GetGUID(), player->GetName(), {}, player->GetRace());
    }

    void OnQueueAddGroup(BattlegroundQueue* queue, GroupQueueInfo* ginfo, uint32& index, Player* /*leader*/, Group* /*grp*/, PvPDifficultyEntry const* /*bracketEntry*/, bool /*isPremade*/) override
    {
        if (!queue || !ginfo || ginfo->Players.empty())
            return;

        if (CONF_GET_BOOL("CFBG.Enable") && !ginfo->ArenaType && !ginfo->IsRated && ginfo->Team == HORDE)
            index++;
    }

    void OnQueueRemovePlayer(BattlegroundQueue* /*queue*/, ObjectGuid /*guid*/, bool /*decreaseInvitedCount*/, uint32& index, uint32& pvpTeamsCount) override
    {
        // Am i missing something, or why is the optimization above made?
        if (CONF_GET_BOOL("CFBG.Enable"))
        {
            index = 0;
            pvpTeamsCount = 1;
        }
    }

    bool CanFillPlayersToBG(BattlegroundQueue* queue, Battleground* bg, const int32 /*aliFree*/, const int32 /*hordeFree*/, BattlegroundBracketId bracket_id) override
    {
        if (sCFBGQueue->MixPlayersToBG(queue, bg, bracket_id))
            return false;

        return true;
    }

    bool CanCheckNormalMatch(BattlegroundQueue* queue, Battleground* bgTemplate, BattlegroundBracketId bracket_id, uint32& minPlayers, uint32& maxPlayers) override
    {
        if (sCFBGQueue->CheckMixedMatch(queue, bgTemplate, bracket_id, minPlayers, maxPlayers))
            return false;

        return true;
    }
};

class CrossFactionBG_Command : public CommandScript
{
public:
    CrossFactionBG_Command() : CommandScript("CrossFactionBG_Command") { }

    Warhead::ChatCommands::ChatCommandTable GetCommands() const override
    {
        static Warhead::ChatCommands::ChatCommandTable debugCommandTable =
        {
            { "startbg", HandleDebugBattlegroundCommand, rbac::RBAC_PERM_COMMAND_DEBUG, Warhead::ChatCommands::Console::No }
        };

        static Warhead::ChatCommands::ChatCommandTable commandTable =
        {
            { "cfbg", debugCommandTable }
        };

        return commandTable;
    }

    static bool HandleDebugBattlegroundCommand(ChatHandler* handler)
    {
        if (!handler->GetSession())
            return false;

        auto bg = handler->GetSession()->GetPlayer()->GetBattleground();

        if (bg)
        {
            bg->SetStartDelayTime(-1);
            handler->SendSysMessage("Battleground (%s) started", bg->GetName().c_str());
        }

        return true;
    }
};

class CrossFactionBG_World : public WorldScript
{
public:
    CrossFactionBG_World() : WorldScript("CrossFactionBG_World") { }

    void OnConfigLoad(bool /*reload*/) override
    {
        sGameConfig->AddOption<bool>("CFBG.Enable");
        sGameConfig->AddOption<bool>("CFBG.ReplaceLanguages");
        sGameConfig->AddOption<bool>("CFBG.ReplaceRacials");
    }
};

// Group all custom scripts
void AddSC_CrossFactionBG()
{
    new CrossFactionBG_BG();
    new CrossFactionBG_Command();
    new CrossFactionBG_World();
}

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

#include "CrossFactionData.h"
#include "GameConfig.h"
#include "Player.h"
#include "Item.h"
#include "WorldSession.h"
#include "World.h"
#include "MiscPackets.h"
#include "ObjectMgr.h"
#include "SpellMgr.h"
#include "SpellInfo.h"
#include "Battleground.h"
#include "CharacterCache.h"
#include "DBCStores.h"

CrossFactionData::CrossFactionData(Player* player)
{
    _player = player;
}

void CrossFactionData::SetCrossFactionData()
{
    if (!CONF_GET_BOOL("CrossFactionData.Enable"))
        return;

    _player->SetByteValue(UNIT_FIELD_BYTES_0, 0, NativeTeam() ? GetOriginalRace() : GetFakeRace());
    _player->SetFaction(NativeTeam() ? GetOriginalFaction() : GetFakeFaction());

    ReplaceRacials();
    SetRaceDisplayID();

    // Calling this in BattleGround::AddPlayer fixes scoreboard
    sCharacterCache->UpdateCharacterData(_player->GetGUID(), _player->GetName(), {}, _player->GetRace());
}

void CrossFactionData::SetRaceDisplayID()
{
    if (!CONF_GET_BOOL("CrossFactionData.Enable"))
        return;

    PlayerInfo const* info = sObjectMgr->GetPlayerInfo(_player->GetRace(), _player->GetClass());
    if (!info)
        return;

    _player->SetObjectScale(1.f);

    switch (_player->GetGender())
    {
    case GENDER_FEMALE:
        _player->SetDisplayId(info->displayId_f);
        _player->SetNativeDisplayId(info->displayId_f);
        break;
    case GENDER_MALE:
        _player->SetDisplayId(info->displayId_m);
        _player->SetNativeDisplayId(info->displayId_m);
        break;
    default:
        return;
    }
}

void CrossFactionData::ReplaceRacials()
{
    if (!CONF_GET_BOOL("CFBG.Enable"))
        return;

    ReplaceItems();

    std::unordered_map<uint16, bool> skills;

    auto UpdateSkills = [&](uint32 prace, uint32 pclass, bool add)
    {
        auto playerInfo = sObjectMgr->GetPlayerInfo(prace, pclass);
        if (!playerInfo)
            return;

        for (auto& i : playerInfo->skills)
        {
            SkillLineEntry const* skill = sSkillLineStore.LookupEntry(i.SkillId);
            if (!skill)
                continue;

            if (CONF_GET_BOOL("CFBG.ReplaceLanguages") && skill->CategoryID != SKILL_CATEGORY_LANGUAGES)
                continue;

            if (CONF_GET_BOOL("CFBG.ReplaceRacials") && skill->CategoryID == SKILL_CATEGORY_LANGUAGES)
                continue;

            skills[i.SkillId] = add;
        }
    };

    for (uint8 i = 0; i < MAX_RACES; ++i)
        UpdateSkills(i, _player->GetClass(), false);

    UpdateSkills(NativeTeam() ? GetOriginalRace() : GetFakeRace(), _player->GetClass(), true);

    for (auto const& skillinfo : skills)
    {
        if (skillinfo.second)
            _player->LearnDefaultSkill(skillinfo.first, 0);
        else
            _player->SetSkill(skillinfo.first, 0, 0, 0);
    }

    _player->SendUpdateToPlayer(_player);
}

void CrossFactionData::ReplaceItems()
{
    if (!CONF_GET_BOOL("CFBG.Enable"))
        return;

    for (auto const& itempair : sObjectMgr->FactionChangeItems)
    {
        uint32 item_alliance = itempair.first;
        uint32 item_horde = itempair.second;

        auto ReplaceItem = [&](uint32 sourceitem, uint32 destinationitem)
        {
            while (Item* item = _player->GetItemByEntry(sourceitem))
            {
                item->SetEntry(destinationitem);
                item->SetState(ITEM_CHANGED);
            }
        };

        if (_player->GetTeam() == ALLIANCE)
            ReplaceItem(item_horde, item_alliance);
        else
            ReplaceItem(item_alliance, item_horde);
    }
}

void CrossFactionData::InitializeData()
{
    _originalRace = _player->GetRace();
    _player->SetFactionForRace(_player->GetRace());
    _originalFaction = _player->GetFaction();
    _originalTeam = Player::TeamForRace(_originalRace);

    _fakeRace = 0;

    while (!_fakeRace)
        for (uint8 i = 0; i < MAX_RACES; ++i)
            if (sObjectMgr->GetPlayerInfo(i, _player->GetClass()) && Player::TeamForRace(i) != GetOriginalTeam() && urand(0, 5) == 0)
                _fakeRace = i;

    ChrRacesEntry const* rEntry = sChrRacesStore.LookupEntry(_fakeRace);
    _fakeFaction = rEntry ? rEntry->FactionID : 0;
    _fakeTeam = Player::TeamForRace(_fakeRace);
}

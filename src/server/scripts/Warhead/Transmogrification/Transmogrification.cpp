﻿/*
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

#include "Transmogrification.h"
#include "Bag.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "GameEventMgr.h"
#include "GameConfig.h"
#include "GameLocale.h"
#include "ModuleLocale.h"
#include "Item.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "WorldSession.h"
#include "World.h"

namespace
{
    constexpr char ALLOWED_SYMBOLS[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyzАБВГДЕЁЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯабвгдеёжзийклмнопрстуфхцчшщъыьэюя _.,'1234567890";
}

enum StringLocales : uint8
{
    // Slots
    TRANSMOG_LOCALE_HEAD = 1,
    TRANSMOG_LOCALE_SHOULDERS,
    TRANSMOG_LOCALE_SHIRT,
    TRANSMOG_LOCALE_CHEST,
    TRANSMOG_LOCALE_WAIST,
    TRANSMOG_LOCALE_LEGS,
    TRANSMOG_LOCALE_FEET,
    TRANSMOG_LOCALE_WRISTS,
    TRANSMOG_LOCALE_HANDS,
    TRANSMOG_LOCALE_BACK,
    TRANSMOG_LOCALE_MAIN_HAND,
    TRANSMOG_LOCALE_OFF_HAND,
    TRANSMOG_LOCALE_RANGED,
    TRANSMOG_LOCALE_TABARD,

    // Gossips
    TRANSMOG_LOCALE_GOSSIP_ITEM_HOW_WORKS,
    TRANSMOG_LOCALE_GOSSIP_ITEM_MANAGE_SETS,
    TRANSMOG_LOCALE_GOSSIP_ITEM_REMOVE_ALL_TRANSMOG,
    TRANSMOG_LOCALE_GOSSIP_ITEM_REMOVE_SINGLE_TRANSMOG,
    TRANSMOG_LOCALE_GOSSIP_ITEM_UPDATE_MENU,
    TRANSMOG_LOCALE_GOSSIP_ITEM_BACK,
    TRANSMOG_LOCALE_GOSSIP_ITEM_REMOVE_ALL_TRANSMOG_Q,
    TRANSMOG_LOCALE_GOSSIP_ITEM_REMOVE_SINGLE_TRANSMOG_Q,
    TRANSMOG_LOCALE_GOSSIP_ITEM_SAVE_SET,
    TRANSMOG_LOCALE_GOSSIP_ITEM_ITEM_BIND_Q,
    TRANSMOG_LOCALE_GOSSIP_ITEM_HOW_SET_WORKS,
    TRANSMOG_LOCALE_GOSSIP_ITEM_SET_USE,
    TRANSMOG_LOCALE_GOSSIP_ITEM_SET_DELETE,
    TRANSMOG_LOCALE_GOSSIP_ITEM_SET_DELETE_Q,
    TRANSMOG_LOCALE_GOSSIP_ITEM_SET_INSERT_NAME,

    // Strings
    TRANSMOG_LOCALE_TRANSMOG_OK,
    TRANSMOG_LOCALE_TRANSMOG_INVALID_SLOT,
    TRANSMOG_LOCALE_TRANSMOG_INVALID_SRC_ENTRY,
    TRANSMOG_LOCALE_TRANSMOG_MISSING_SRC_ITEM,
    TRANSMOG_LOCALE_TRANSMOG_MISSING_DEST_ITEM,
    TRANSMOG_LOCALE_TRANSMOG_INVALID_ITEMS,
    TRANSMOG_LOCALE_TRANSMOG_NOT_ENOUGH_MONEY,
    TRANSMOG_LOCALE_TRANSMOG_NOT_ENOUGH_TOKENS,
    TRANSMOG_LOCALE_UNTRANSMOG_OK,
    TRANSMOG_LOCALE_UNTRANSMOG_NO_TRANSMOGS,
    TRANSMOG_LOCALE_PRESET_ERR_INVALID_NAME,

    TRANSMOG_LOCALE_MAX
};

#define MODULE_NAME "mod-transmogrification"

Transmogrification* Transmogrification::instance()
{
    static Transmogrification instance;
    return &instance;
}

void Transmogrification::Init()
{
    sModuleLocale->CheckStrings(MODULE_NAME, TRANSMOG_LOCALE_MAX);

    uint32 tokenEntry = CONF_GET_INT("Transmogrification.TokenEntry");

    if (!sObjectMgr->GetItemTemplate(tokenEntry))
    {
        LOG_ERROR("module.transmog", "Transmogrification.TokenEntry (%u) does not exist. Using default.", tokenEntry);
        sGameConfig->SetOption<int32>("Transmogrification.TokenEntry", 49426);
    }
}

void Transmogrification::PresetTransmog(Player* player, Item* itemTransmogrified, uint32 fakeEntry, uint8 slot)
{
    if (!CONF_GET_BOOL("Transmogrification.EnableSets"))
        return;

    if (!player || !itemTransmogrified)
        return;

    if (slot >= EQUIPMENT_SLOT_END)
        return;

    if (!CanTransmogrifyItemWithItem(player, itemTransmogrified->GetTemplate(), sObjectMgr->GetItemTemplate(fakeEntry)))
        return;

    if (GetFakeEntry(itemTransmogrified->GetGUID()))
        DeleteFakeEntry(player, slot, itemTransmogrified);

    SetFakeEntry(player, fakeEntry, slot, itemTransmogrified);

    itemTransmogrified->UpdatePlayedTime(player);
    itemTransmogrified->SetOwnerGUID(player->GetGUID());
    itemTransmogrified->SetNotRefundable(player);
    itemTransmogrified->ClearSoulboundTradeable(player);
    itemTransmogrified->SetState(ITEM_CHANGED, player);

    UpdateItem(player, itemTransmogrified);
}

void Transmogrification::LoadPlayerSets(Player* player)
{
    auto playerGUID = player->GetGUID();

    for (auto itr : _presetById[playerGUID])
        itr.second.clear();

    _presetById[playerGUID].clear();
    _presetByName[playerGUID].clear();

    QueryResult result = CharacterDatabase.PQuery("SELECT `PresetID`, `SetName`, `SetData` FROM `custom_transmogrification_sets` WHERE Owner = %u", player->GetGUID().GetCounter());
    if (!result)
        return;

    do
    {
        Field* field = result->Fetch();
        uint8 PresetID = field[0].GetUInt8();
        std::string SetName = field[1].GetString();
        std::istringstream SetData(field[2].GetString());

        while (SetData.good())
        {
            uint32 slot;
            uint32 entry;

            SetData >> slot >> entry;
            if (SetData.fail())
                break;

            if (slot >= EQUIPMENT_SLOT_END)
            {
                LOG_ERROR("custom.transmog", "Item entry (FakeEntry: %u, playerGUID: %u, slot: %u, presetId: %u) has invalid slot, ignoring.", entry, player->GetGUID().GetCounter(), uint32(slot), uint32(PresetID));
                continue;
            }

            if (sObjectMgr->GetItemTemplate(entry))
                _presetById[playerGUID][PresetID][slot] = entry;
            else
                LOG_ERROR("custom.transmog", "Item entry (FakeEntry: %u, playerGUID: %u, slot: %u, presetId: %u) does not exist, ignoring.", entry, player->GetGUID().GetCounter(), slot, PresetID);

            if (!_presetById[playerGUID][PresetID].empty())
                _presetByName[playerGUID][PresetID] = SetName;
            else // should be deleted on startup, so  this never runs (shouldnt..)
            {
                _presetById[playerGUID].erase(PresetID);
                CharacterDatabase.PExecute("DELETE FROM `custom_transmogrification_sets` WHERE Owner = %u AND PresetID = %u", playerGUID.GetCounter(), PresetID);
            }
        }
    } while (result->NextRow());
}

void Transmogrification::UnloadPlayerSets(Player* player)
{
    auto playerGUID = player->GetGUID();

    for (auto itr : _presetById[playerGUID])
        itr.second.clear();

    _presetById[playerGUID].clear();
    _presetByName[playerGUID].clear();
}

std::string const Transmogrification::GetSlotName(Player* player, uint8 slot) const
{
    if (!CanTransmogSlot(slot))
        return "";

    uint8 localeIndex = static_cast<uint8>(player->GetSession()->GetSessionDbLocaleIndex());

    switch (slot)
    {
    case EQUIPMENT_SLOT_HEAD:
        return sModuleLocale->GetModuleString(MODULE_NAME, TRANSMOG_LOCALE_HEAD, localeIndex).value_or("Head"); // Head
    case EQUIPMENT_SLOT_SHOULDERS:
        return sModuleLocale->GetModuleString(MODULE_NAME, TRANSMOG_LOCALE_SHOULDERS, localeIndex).value_or("Shoulders"); // Shoulders
    case EQUIPMENT_SLOT_BODY:
        return sModuleLocale->GetModuleString(MODULE_NAME, TRANSMOG_LOCALE_SHIRT, localeIndex).value_or("Shirt"); // Shirt
    case EQUIPMENT_SLOT_CHEST:
        return sModuleLocale->GetModuleString(MODULE_NAME, TRANSMOG_LOCALE_CHEST, localeIndex).value_or("Chest"); // Chest
    case EQUIPMENT_SLOT_WAIST:
        return sModuleLocale->GetModuleString(MODULE_NAME, TRANSMOG_LOCALE_WAIST, localeIndex).value_or("Waist"); // Waist
    case EQUIPMENT_SLOT_LEGS:
        return sModuleLocale->GetModuleString(MODULE_NAME, TRANSMOG_LOCALE_LEGS, localeIndex).value_or("Legs"); // Legs
    case EQUIPMENT_SLOT_FEET:
        return sModuleLocale->GetModuleString(MODULE_NAME, TRANSMOG_LOCALE_FEET, localeIndex).value_or("Feet"); // Feet
    case EQUIPMENT_SLOT_WRISTS:
        return sModuleLocale->GetModuleString(MODULE_NAME, TRANSMOG_LOCALE_WRISTS, localeIndex).value_or("Wrists"); // Wrists
    case EQUIPMENT_SLOT_HANDS:
        return sModuleLocale->GetModuleString(MODULE_NAME, TRANSMOG_LOCALE_HANDS, localeIndex).value_or("Hands"); // Hands
    case EQUIPMENT_SLOT_BACK:
        return sModuleLocale->GetModuleString(MODULE_NAME, TRANSMOG_LOCALE_BACK, localeIndex).value_or("Back"); // Back
    case EQUIPMENT_SLOT_MAINHAND:
        return sModuleLocale->GetModuleString(MODULE_NAME, TRANSMOG_LOCALE_MAIN_HAND, localeIndex).value_or("Main hand"); // Main hand
    case EQUIPMENT_SLOT_OFFHAND:
        return sModuleLocale->GetModuleString(MODULE_NAME, TRANSMOG_LOCALE_OFF_HAND, localeIndex).value_or("Off hand"); // Off hand
    case EQUIPMENT_SLOT_RANGED:
        return sModuleLocale->GetModuleString(MODULE_NAME, TRANSMOG_LOCALE_RANGED, localeIndex).value_or("Ranged"); // Ranged
    case EQUIPMENT_SLOT_TABARD:
        return sModuleLocale->GetModuleString(MODULE_NAME, TRANSMOG_LOCALE_TABARD, localeIndex).value_or("Tabard"); // Tabard
    default:
    {
        LOG_FATAL("module.transmog", "> Transmog: unknown slot (%u)", slot);
        return "";
    }
    }
}

std::string Transmogrification::GetItemIcon(uint32 entry, uint32 width, uint32 height, int x, int y) const
{
    std::ostringstream ss;
    ss << "|TInterface";

    const ItemDisplayInfoEntry* dispInfo = nullptr;

    if (const ItemTemplate* temp = sObjectMgr->GetItemTemplate(entry))
    {
        dispInfo = sItemDisplayInfoStore.LookupEntry(temp->DisplayInfoID);
        if (dispInfo)
            ss << "/ICONS/" << dispInfo->InventoryIcon[0];
    }

    if (!dispInfo)
        ss << "/InventoryItems/WoWUnknownItem01";

    ss << ":" << width << ":" << height << ":" << x << ":" << y << "|t";

    return ss.str();
}

std::string Transmogrification::GetSlotIcon(uint8 slot, uint32 width, uint32 height, int x, int y) const
{
    std::ostringstream ss;
    ss << "|TInterface/PaperDoll/";

    switch (slot)
    {
    case EQUIPMENT_SLOT_HEAD:
        ss << "UI-PaperDoll-Slot-Head";
        break;
    case EQUIPMENT_SLOT_SHOULDERS:
        ss << "UI-PaperDoll-Slot-Shoulder";
        break;
    case EQUIPMENT_SLOT_BODY:
        ss << "UI-PaperDoll-Slot-Shirt";
        break;
    case EQUIPMENT_SLOT_CHEST:
        ss << "UI-PaperDoll-Slot-Chest";
        break;
    case EQUIPMENT_SLOT_WAIST:
        ss << "UI-PaperDoll-Slot-Waist";
        break;
    case EQUIPMENT_SLOT_LEGS:
        ss << "UI-PaperDoll-Slot-Legs";
        break;
    case EQUIPMENT_SLOT_FEET:
        ss << "UI-PaperDoll-Slot-Feet";
        break;
    case EQUIPMENT_SLOT_WRISTS:
        ss << "UI-PaperDoll-Slot-Wrists";
        break;
    case EQUIPMENT_SLOT_HANDS:
        ss << "UI-PaperDoll-Slot-Hands";
        break;
    case EQUIPMENT_SLOT_BACK:
        ss << "UI-PaperDoll-Slot-Chest";
        break;
    case EQUIPMENT_SLOT_MAINHAND:
        ss << "UI-PaperDoll-Slot-MainHand";
        break;
    case EQUIPMENT_SLOT_OFFHAND:
        ss << "UI-PaperDoll-Slot-SecondaryHand";
        break;
    case EQUIPMENT_SLOT_RANGED:
        ss << "UI-PaperDoll-Slot-Ranged";
        break;
    case EQUIPMENT_SLOT_TABARD:
        ss << "UI-PaperDoll-Slot-Tabard";
        break;
    default:
        ss << "UI-Backpack-EmptySlot";
    }

    ss << ":" << width << ":" << height << ":" << x << ":" << y << "|t";

    return ss.str();
}

std::string Transmogrification::GetItemLink(Item* item, WorldSession* session) const
{
    LocaleConstant loc_idx = session->GetSessionDbLocaleIndex();
    const ItemTemplate* temp = item->GetTemplate();
    std::string name = temp->Name1;

    if (ItemLocale const* il = sGameLocale->GetItemLocale(temp->ItemId))
        sGameLocale->GetLocaleString(il->Name, loc_idx, name);

    if (int32 itemRandPropId = item->GetItemRandomPropertyId())
    {
        std::array<char const*, 16> const* suffix = nullptr;
        if (itemRandPropId < 0)
        {
            if (const ItemRandomSuffixEntry* itemRandEntry = sItemRandomSuffixStore.LookupEntry(-itemRandPropId))
                suffix = &itemRandEntry->Name;
        }
        else
        {
            if (const ItemRandomPropertiesEntry* itemRandEntry = sItemRandomPropertiesStore.LookupEntry(itemRandPropId))
                suffix = &itemRandEntry->Name;
        }

        if (suffix)
        {
            std::string_view test((*suffix)[(name != temp->Name1) ? loc_idx : DEFAULT_LOCALE]);
            if (!test.empty())
            {
                name += ' ';
                name += test;
            }
        }
    }

    std::ostringstream oss;
    oss << "|c" << std::hex << ItemQualityColors[temp->Quality] << std::dec <<
        "|Hitem:" << temp->ItemId << ":" <<
        item->GetEnchantmentId(PERM_ENCHANTMENT_SLOT) << ":" <<
        item->GetEnchantmentId(SOCK_ENCHANTMENT_SLOT) << ":" <<
        item->GetEnchantmentId(SOCK_ENCHANTMENT_SLOT_2) << ":" <<
        item->GetEnchantmentId(SOCK_ENCHANTMENT_SLOT_3) << ":" <<
        item->GetEnchantmentId(BONUS_ENCHANTMENT_SLOT) << ":" <<
        item->GetItemRandomPropertyId() << ":" << item->GetItemSuffixFactor() << ":" <<
        (uint32)item->GetOwner()->GetLevel() << "|h[" << name << "]|h|r";

    return oss.str();
}

std::string Transmogrification::GetItemLink(uint32 entry, WorldSession* session) const
{
    const ItemTemplate* temp = sObjectMgr->GetItemTemplate(entry);

    std::string name = temp->Name1;
    if (ItemLocale const* il = sGameLocale->GetItemLocale(entry))
        sGameLocale->GetLocaleString(il->Name, session->GetSessionDbLocaleIndex(), name);

    std::ostringstream oss;
    oss << "|c" << std::hex << ItemQualityColors[temp->Quality] << std::dec <<
        "|Hitem:" << entry << ":0:0:0:0:0:0:0:0:0|h[" << name << "]|h|r";

    return oss.str();
}

uint32 Transmogrification::GetFakeEntry(ObjectGuid itemGUID) const
{
    auto const& itr = _dataMapStore.find(itemGUID);
    if (itr == _dataMapStore.end())
        return 0;

    auto const& itr2 = _mapStore.find(itr->second);
    if (itr2 == _mapStore.end())
        return 0;

    auto const& itr3 = itr2->second.find(itemGUID);
    if (itr3 == itr2->second.end())
        return 0;

    return itr3->second;
}

void Transmogrification::UpdateItem(Player* player, Item* item) const
{
    if (item->IsEquipped())
    {
        player->SetVisibleItemSlot(item->GetSlot(), item);

        if (player->IsInWorld())
            item->SendUpdateToPlayer(player);
    }
}

void Transmogrification::DeleteFakeEntry(Player* player, uint8 /*slot*/, Item* itemTransmogrified)
{
    DeleteFakeFromDB(itemTransmogrified->GetGUID().GetCounter());
    UpdateItem(player, itemTransmogrified);
}

void Transmogrification::SetFakeEntry(Player* player, uint32 newEntry, uint8 /*slot*/, Item* itemTransmogrified)
{
    auto itemGUID = itemTransmogrified->GetGUID();

    _mapStore[player->GetGUID()][itemGUID] = newEntry;
    _dataMapStore[itemGUID] = player->GetGUID();

    CharacterDatabase.PExecute("REPLACE INTO custom_transmogrification (GUID, FakeEntry, Owner) VALUES (%u, %u, %u)",
                               itemTransmogrified->GetGUID().GetCounter(), newEntry, player->GetGUID().GetCounter());

    UpdateItem(player, itemTransmogrified);
}

void Transmogrification::Transmogrify(Player* player, ObjectGuid itemGUID, uint8 slot, bool no_cost)
{
    // slot of the transmogrified item
    if (slot >= EQUIPMENT_SLOT_END)
    {
        LOG_DEBUG("custom.transmog", "Transmogrification::Transmogrify - %s (%s) tried to transmogrify an %s with a wrong slot (%u) when transmogrifying items.", player->GetName().c_str(),
                  player->GetGUID().ToString().c_str(), itemGUID.ToString().c_str(), slot);
        SendNotification(player, TRANSMOG_LOCALE_TRANSMOG_INVALID_SLOT);
        return;
    }

    Item* itemTransmogrifier = nullptr;

    // guid of the transmogrifier item, if it's not 0
    if (!itemGUID.IsEmpty())
    {
        itemTransmogrifier = player->GetItemByGuid(itemGUID);
        if (!itemTransmogrifier)
        {
            LOG_DEBUG("custom.transmog", "Transmogrification::Transmogrify - %s (%s) tried to transmogrify with an invalid %s.", player->GetName().c_str(), player->GetGUID().ToString().c_str(),
                      itemGUID.ToString().c_str());
            SendNotification(player, TRANSMOG_LOCALE_TRANSMOG_MISSING_SRC_ITEM);
            return;
        }
    }

    // transmogrified item
    Item* itemTransmogrified = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
    if (!itemTransmogrified)
    {
        LOG_DEBUG("custom.transmog", "Transmogrification::Transmogrify - %s (%s) tried to transmogrify an invalid item in a valid slot (slot: %u).", player->GetName().c_str(),
                  player->GetGUID().ToString().c_str(), slot);
        SendNotification(player, TRANSMOG_LOCALE_TRANSMOG_MISSING_DEST_ITEM);
        return;
    }

    if (!itemTransmogrifier) // reset look newEntry
    {
        DeleteFakeEntry(player, slot, itemTransmogrified);
        itemTransmogrified->SetState(ITEM_CHANGED, player);
    }
    else
    {
        if (!CanTransmogrifyItemWithItem(player, itemTransmogrified->GetTemplate(), itemTransmogrifier->GetTemplate()))
        {
            LOG_DEBUG("custom.transmog", "Transmogrification::Transmogrify - %s (%s) failed CanTransmogrifyItemWithItem (%u with %u).", player->GetName().c_str(), player->GetGUID().ToString().c_str(),
                      itemTransmogrified->GetEntry(), itemTransmogrifier->GetEntry());
            SendNotification(player, TRANSMOG_LOCALE_TRANSMOG_INVALID_ITEMS);
            return;
        }

        if (!no_cost)
        {
            if (CONF_GET_BOOL("Transmogrification.RequireToken"))
            {
                if (player->HasItemCount(CONF_GET_INT("Transmogrification.TokenEntry"), CONF_GET_INT("Transmogrification.TokenAmount")))
                    player->DestroyItemCount(CONF_GET_INT("Transmogrification.TokenEntry"), CONF_GET_INT("Transmogrification.TokenAmount"), true);
                else
                {
                    SendNotification(player, TRANSMOG_LOCALE_TRANSMOG_NOT_ENOUGH_TOKENS);
                    return;
                }
            }

            int32 cost = 0;
            cost = GetSpecialPrice(itemTransmogrified->GetTemplate());
            cost *= CONF_GET_FLOAT("Transmogrification.ScaledCostModifier");
            cost += CONF_GET_INT("Transmogrification.CopperCost");

            if (cost) // 0 cost if reverting look
            {
                if (cost < 0)
                    LOG_DEBUG("custom.transmog", "Transmogrification::Transmogrify - %s (%s) transmogrification invalid cost (non negative, amount %i). Transmogrified %u with %u", player->GetName().c_str(),
                              player->GetGUID().ToString().c_str(), -cost, itemTransmogrified->GetEntry(), itemTransmogrifier->GetEntry());
                else
                {
                    if (!player->HasEnoughMoney(cost))
                    {
                        SendNotification(player, TRANSMOG_LOCALE_TRANSMOG_NOT_ENOUGH_MONEY);
                        return;
                    }

                    player->ModifyMoney(-cost, false);
                }
            }
        }

        SetFakeEntry(player, itemTransmogrifier->GetEntry(), slot, itemTransmogrified);

        itemTransmogrified->UpdatePlayedTime(player);
        itemTransmogrified->SetOwnerGUID(player->GetGUID());
        itemTransmogrified->SetNotRefundable(player);
        itemTransmogrified->ClearSoulboundTradeable(player);

        if (itemTransmogrifier->GetTemplate()->Bonding == BIND_WHEN_EQUIPED || itemTransmogrifier->GetTemplate()->Bonding == BIND_WHEN_PICKED_UP
                || itemTransmogrifier->GetTemplate()->Bonding == BIND_QUEST_ITEM || itemTransmogrifier->GetTemplate()->Bonding == BIND_WHEN_USE)
            itemTransmogrifier->SetBinding(true);

        itemTransmogrifier->SetOwnerGUID(player->GetGUID());
        itemTransmogrifier->SetNotRefundable(player);
        itemTransmogrifier->ClearSoulboundTradeable(player);
        itemTransmogrifier->SetState(ITEM_CHANGED, player);
        itemTransmogrified->SetState(ITEM_CHANGED, player);
    }

    SendNotification(player, TRANSMOG_LOCALE_TRANSMOG_OK);
}

bool Transmogrification::CanTransmogrifyItemWithItem(Player* player, ItemTemplate const* target, ItemTemplate const* source) const
{
    if (!target || !source)
        return false;

    if (source->ItemId == target->ItemId)
        return false;

    if (source->DisplayInfoID == target->DisplayInfoID)
        return false;

    if (source->Class != target->Class)
        return false;

    if (source->InventoryType == INVTYPE_BAG ||
            source->InventoryType == INVTYPE_RELIC ||
            // source->InventoryType == INVTYPE_BODY ||
            source->InventoryType == INVTYPE_FINGER ||
            source->InventoryType == INVTYPE_TRINKET ||
            source->InventoryType == INVTYPE_AMMO ||
            source->InventoryType == INVTYPE_QUIVER)
        return false;

    if (target->InventoryType == INVTYPE_BAG ||
            target->InventoryType == INVTYPE_RELIC ||
            // target->InventoryType == INVTYPE_BODY ||
            target->InventoryType == INVTYPE_FINGER ||
            target->InventoryType == INVTYPE_TRINKET ||
            target->InventoryType == INVTYPE_AMMO ||
            target->InventoryType == INVTYPE_QUIVER)
        return false;

    if (!SuitableForTransmogrification(player, target) || !SuitableForTransmogrification(player, source)) // if (!transmogrified->CanTransmogrify() || !transmogrifier->CanBeTransmogrified())
        return false;

    if (IsRangedWeapon(source->Class, source->SubClass) != IsRangedWeapon(target->Class, target->SubClass))
        return false;

    if (source->SubClass != target->SubClass && !IsRangedWeapon(target->Class, target->SubClass))
    {
        if (source->Class == ITEM_CLASS_ARMOR && !CONF_GET_BOOL("Transmogrification.AllowMixedArmorTypes"))
            return false;

        if (source->Class == ITEM_CLASS_WEAPON && !player->GetSkillValue(target->GetSkill()))
            return false;
    }

    if (!CONF_GET_BOOL("Transmogrification.AllowMixedWeaponTypes") && source->InventoryType != target->InventoryType)
    {
        if (target->Class != ITEM_CLASS_WEAPON || source->Class != ITEM_CLASS_WEAPON)
            return false;

        if (source->InventoryType == INVTYPE_2HWEAPON || target->InventoryType == INVTYPE_2HWEAPON)
            return false;
    }

    if (source->InventoryType != target->InventoryType)
    {
        if (source->Class == ITEM_CLASS_WEAPON && !(IsRangedWeapon(target->Class, target->SubClass) ||
                ((
                     target->InventoryType == INVTYPE_WEAPON ||
                     target->InventoryType == INVTYPE_2HWEAPON ||
                     target->InventoryType == INVTYPE_WEAPONMAINHAND ||
                     target->InventoryType == INVTYPE_WEAPONOFFHAND)
                 && (
                     source->InventoryType == INVTYPE_WEAPON ||
                     source->InventoryType == INVTYPE_2HWEAPON ||
                     source->InventoryType == INVTYPE_WEAPONMAINHAND ||
                     source->InventoryType == INVTYPE_WEAPONOFFHAND))
                                                   ))
            return false;

        if (source->Class == ITEM_CLASS_ARMOR &&
                !((source->InventoryType == INVTYPE_CHEST || source->InventoryType == INVTYPE_ROBE) &&
                  (target->InventoryType == INVTYPE_CHEST || target->InventoryType == INVTYPE_ROBE)))
            return false;
    }

    return true;
}

bool Transmogrification::SuitableForTransmogrification(Player* player, ItemTemplate const* proto) const
{
    if (!player || !proto)
        return false;

    if (proto->Class != ITEM_CLASS_ARMOR &&
            proto->Class != ITEM_CLASS_WEAPON)
        return false;

    // Skip all checks for allowed items
    if (IsAllowed(proto->ItemId))
        return true;

    /*if (proto->SubClass && !player->GetSkillValue(proto->GetSkill()))
    {
        if (proto->Class == ITEM_CLASS_ARMOR && !CONF_GET_BOOL("Transmogrification.AllowMixedArmorTypes"))
            return false;
        else if (proto->Class == ITEM_CLASS_WEAPON && !CONF_GET_BOOL("Transmogrification.AllowMixedWeaponTypes"))
            return false;
        else
            return false;
    }*/

    if (IsNotAllowed(proto->ItemId))
        return false;

    if (!CONF_GET_BOOL("Transmogrification.AllowFishingPoles") && proto->Class == ITEM_CLASS_WEAPON && proto->SubClass == ITEM_SUBCLASS_WEAPON_FISHING_POLE)
        return false;

    if (!IsAllowedQuality(proto->Quality)) // (proto->Quality == ITEM_QUALITY_LEGENDARY)
        return false;

    if ((proto->Flags2 & ITEM_FLAG2_FACTION_HORDE) && player->GetTeam() != HORDE)
        return false;

    if ((proto->Flags2 & ITEM_FLAG2_FACTION_ALLIANCE) && player->GetTeam() != ALLIANCE)
        return false;

    if (!CONF_GET_BOOL("Transmogrification.IgnoreReqClass") && (proto->AllowableClass & player->GetClassMask()) == 0)
        return false;

    if (!CONF_GET_BOOL("Transmogrification.IgnoreReqRace") && (proto->AllowableRace & player->GetRaceMask()) == 0)
        return false;

    if (!CONF_GET_BOOL("Transmogrification.IgnoreReqSkill") && proto->RequiredSkill)
    {
        if (!player->GetSkillValue(proto->RequiredSkill))
            return false;
        else if (player->GetSkillValue(proto->RequiredSkill) < proto->RequiredSkillRank)
            return false;
    }

    if (!CONF_GET_BOOL("Transmogrification.IgnoreReqSpell") && proto->RequiredSpell && !player->HasSpell(proto->RequiredSpell))
        return false;

    if (!CONF_GET_BOOL("Transmogrification.IgnoreReqLevel") && player->GetLevel() < proto->RequiredLevel)
        return false;

    // If World Event is not active, prevent using event dependant items
    if (!CONF_GET_BOOL("Transmogrification.IgnoreReqEvent") && proto->HolidayId && !IsHolidayActive((HolidayIds)proto->HolidayId))
        return false;

    if (!CONF_GET_BOOL("Transmogrification.IgnoreReqStats"))
    {
        if (!proto->RandomProperty && !proto->RandomSuffix && proto->StatsCount)
        {
            bool found = false;

            for (uint8 i = 0; i < proto->StatsCount; ++i)
            {
                if (proto->ItemStat[i].ItemStatValue)
                {
                    found = true;
                    break;
                }
            }

            if (!found)
                return false;
        }
    }

    return true;
}

uint32 Transmogrification::GetSpecialPrice(ItemTemplate const* proto) const
{
    return proto->SellPrice < 10000 ? 10000 : proto->SellPrice;
}

bool Transmogrification::IsRangedWeapon(uint32 Class, uint32 SubClass) const
{
    return Class == ITEM_CLASS_WEAPON && (
               SubClass == ITEM_SUBCLASS_WEAPON_BOW ||
               SubClass == ITEM_SUBCLASS_WEAPON_GUN ||
               SubClass == ITEM_SUBCLASS_WEAPON_CROSSBOW);
}

bool Transmogrification::IsAllowed(uint32 entry) const
{
    return Allowed.find(entry) != Allowed.end();
}

bool Transmogrification::IsNotAllowed(uint32 entry) const
{
    return NotAllowed.find(entry) != NotAllowed.end();
}

bool Transmogrification::IsAllowedQuality(uint32 quality) const
{
    switch (quality)
    {
    case ITEM_QUALITY_POOR:
        return CONF_GET_BOOL("Transmogrification.AllowPoor");
    case ITEM_QUALITY_NORMAL:
        return CONF_GET_BOOL("Transmogrification.AllowCommon");
    case ITEM_QUALITY_UNCOMMON:
        return CONF_GET_BOOL("Transmogrification.AllowUncommon");
    case ITEM_QUALITY_RARE:
        return CONF_GET_BOOL("Transmogrification.AllowRare");
    case ITEM_QUALITY_EPIC:
        return CONF_GET_BOOL("Transmogrification.AllowEpic");
    case ITEM_QUALITY_LEGENDARY:
        return CONF_GET_BOOL("Transmogrification.AllowLegendary");
    case ITEM_QUALITY_ARTIFACT:
        return CONF_GET_BOOL("Transmogrification.AllowArtifact");
    case ITEM_QUALITY_HEIRLOOM:
        return CONF_GET_BOOL("Transmogrification.AllowHeirloom");
    default:
        return false;
    }
}

void Transmogrification::LoadConfig(bool reload)
{
    if (CONF_GET_INT("Transmogrification.MaxSets") > MAX_OPTIONS)
        sGameConfig->SetOption<int32>("Transmogrification.MaxSets", MAX_OPTIONS);

    std::istringstream issAllowed(CONF_GET_STR("Transmogrification.Allowed"));
    std::istringstream issNotAllowed(CONF_GET_STR("Transmogrification.NotAllowed"));

    while (issAllowed.good())
    {
        uint32 entry;
        issAllowed >> entry;

        if (issAllowed.fail())
            break;

        Allowed.emplace(entry);
    }

    while (issNotAllowed.good())
    {
        uint32 entry;
        issNotAllowed >> entry;

        if (issNotAllowed.fail())
            break;

        NotAllowed.emplace(entry);
    }

    if (reload) // dont store presets for nothing
    {
        for (auto const& itr : sWorld->GetAllSessions())
        {
            if (Player* player = itr.second->GetPlayer())
            {
                // skipping session check
                UnloadPlayerSets(player);

                if (CONF_GET_BOOL("Transmogrification.EnableSets"))
                    LoadPlayerSets(player);
            }
        }
    }

    if (!reload)
    {
        LOG_DEBUG("module.transmog", "Deleting non-existing transmogrification entries...");
        CharacterDatabase.DirectExecute("DELETE FROM custom_transmogrification WHERE NOT EXISTS (SELECT 1 FROM item_instance WHERE item_instance.guid = custom_transmogrification.GUID)");

        // Clean even if disabled
        // Dont delete even if player has more presets than should
        CharacterDatabase.DirectExecute("DELETE FROM `custom_transmogrification_sets` WHERE NOT EXISTS(SELECT 1 FROM characters WHERE characters.guid = custom_transmogrification_sets.Owner)");
    }
}

/*std::vector<ObjectGuid> Transmogrification::GetItemList(const Player* player) const
{
    std::vector<ObjectGuid> itemlist;

    // Copy paste from Player::GetItemByGuid(guid)

    for (uint8 i = EQUIPMENT_SLOT_START; i < INVENTORY_SLOT_ITEM_END; ++i)
        if (Item* pItem = player->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            itemlist.push_back(pItem->GetGUID());

    for (uint8 i = KEYRING_SLOT_START; i < CURRENCYTOKEN_SLOT_END; ++i)
        if (Item* pItem = player->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            itemlist.push_back(pItem->GetGUID());

    for (int i = BANK_SLOT_ITEM_START; i < BANK_SLOT_BAG_END; ++i)
        if (Item* pItem = player->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            itemlist.push_back(pItem->GetGUID());

    for (uint8 i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; ++i)
        if (Bag* pBag = player->GetBagByPos(i))
            for (uint32 j = 0; j < pBag->GetBagSize(); ++j)
                if (Item* pItem = pBag->GetItemByPos(j))
                    itemlist.push_back(pItem->GetGUID());

    for (uint8 i = BANK_SLOT_BAG_START; i < BANK_SLOT_BAG_END; ++i)
        if (Bag* pBag = player->GetBagByPos(i))
            for (uint32 j = 0; j < pBag->GetBagSize(); ++j)
                if (Item* pItem = pBag->GetItemByPos(j))
                    itemlist.push_back(pItem->GetGUID());

    return itemlist;
}*/

void Transmogrification::OnPlayerDelete(ObjectGuid guid)
{
    CharacterDatabase.PExecute("DELETE FROM `custom_transmogrification_sets` WHERE `Owner` = %u", guid.GetCounter());
}

void Transmogrification::DeleteFakeFromDB(ObjectGuid::LowType itemGuid)
{
    if (_dataMapStore.find(itemGuid) != _dataMapStore.end())
    {
        if (_mapStore.find(_dataMapStore[itemGuid]) != _mapStore.end())
            _mapStore[_dataMapStore[itemGuid]].erase(itemGuid);

        _dataMapStore.erase(itemGuid);
    }

    CharacterDatabase.PExecute("DELETE FROM custom_transmogrification WHERE GUID = %u", itemGuid);
}

bool Transmogrification::CanTransmogSlot(uint8 slot) const
{
    if (slot == EQUIPMENT_SLOT_HEAD ||
            slot == EQUIPMENT_SLOT_SHOULDERS ||
            slot == EQUIPMENT_SLOT_BODY ||
            slot == EQUIPMENT_SLOT_CHEST ||
            slot == EQUIPMENT_SLOT_WAIST ||
            slot == EQUIPMENT_SLOT_LEGS ||
            slot == EQUIPMENT_SLOT_FEET ||
            slot == EQUIPMENT_SLOT_WRISTS ||
            slot == EQUIPMENT_SLOT_HANDS ||
            slot == EQUIPMENT_SLOT_BACK ||
            slot == EQUIPMENT_SLOT_MAINHAND ||
            slot == EQUIPMENT_SLOT_OFFHAND ||
            slot == EQUIPMENT_SLOT_RANGED ||
            slot == EQUIPMENT_SLOT_TABARD)
        return true;

    return false;
}

void Transmogrification::ClearPlayerAtLogout(Player* player)
{
    if (!player)
        return;

    auto pGUID = player->GetGUID();

    for (auto const& itr : _mapStore[pGUID])
        _dataMapStore.erase(itr.first);

    _mapStore.erase(pGUID);

    if (CONF_GET_BOOL("Transmogrification.EnableSets"))
        UnloadPlayerSets(player);
}

void Transmogrification::LoadPlayerAtLogin(Player* player)
{
    if (!player)
        return;

    auto playerGUID = player->GetGUID();

    _mapStore.erase(playerGUID);

    QueryResult result = CharacterDatabase.PQuery("SELECT GUID, FakeEntry FROM custom_transmogrification WHERE Owner = %u", player->GetGUID().GetCounter());
    if (result)
    {
        do
        {
            Field* field = result->Fetch();
            ObjectGuid itemGUID(HighGuid::Item, 0, field[0].GetUInt32());
            uint32 fakeEntry = (*result)[1].GetUInt32();

            if (sObjectMgr->GetItemTemplate(fakeEntry) && player->GetItemByGuid(itemGUID))
            {
                _dataMapStore[itemGUID] = playerGUID;
                _mapStore[playerGUID][itemGUID] = fakeEntry;
            }
        } while (result->NextRow());

        for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
        {
            if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                player->SetVisibleItemSlot(slot, item);
        }
    }

    if (CONF_GET_BOOL("Transmogrification.EnableSets"))
        LoadPlayerSets(player);
}

std::string const Transmogrification::GetGossipIcon(uint8 slot, Player* player)
{
    auto const& slotName = GetSlotName(player, slot);
    if (slotName.empty())
        return "";

    Item* newItem = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
    uint32 entry = newItem ? GetFakeEntry(newItem->GetGUID()) : 0;
    std::string icon = entry ? GetItemIcon(entry, 30, 30, -18, 0) : GetSlotIcon(slot, 30, 30, -18, 0);

    return icon + slotName;
}

std::string const Transmogrification::GetGossipItemName(Player* player, uint8 gossipItem)
{
    return sModuleLocale->GetModuleString(MODULE_NAME, gossipItem, static_cast<uint8>(player->GetSession()->GetSessionDbLocaleIndex())).value_or("");
}

bool Transmogrification::CanSavePresets(Player* player)
{
    return CONF_GET_BOOL("Transmogrification.EnableSets") && static_cast<uint32>(_presetByName[player->GetGUID()].size()) < CONF_GET_UINT("Transmogrification.MaxSets");
}

void Transmogrification::SavePreset(Player* player, std::string const& name)
{
    if (name.empty() || name.find_first_not_of(ALLOWED_SYMBOLS) != std::string::npos)
    {
        SendNotification(player, TRANSMOG_LOCALE_PRESET_ERR_INVALID_NAME);
        return;
    }

    uint8 maxSets = static_cast<uint8>(CONF_GET_INT("Transmogrification.MaxSets"));

    for (uint8 presetID = 0; presetID < maxSets; ++presetID) // should never reach over max
    {
        if (_presetByName[player->GetGUID()].find(presetID) != _presetByName[player->GetGUID()].end())
            continue; // Just remember never to use _presetByName[pGUID][presetID] when finding etc!

        int32 cost = 0;
        std::map<uint8, uint32> items;

        for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
        {
            if (GetSlotName(player, slot).empty())
                continue;

            if (Item* newItem = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            {
                uint32 entry = GetFakeEntry(newItem->GetGUID());
                if (!entry)
                    continue;

                const ItemTemplate* temp = sObjectMgr->GetItemTemplate(entry);

                if (!temp)
                    continue;

                if (!SuitableForTransmogrification(player, temp))
                    continue;

                cost += GetSpecialPrice(temp);
                items[slot] = entry;
            }
        }

        if (items.empty())
            break; // no transmogrified items were found to be saved

        cost *= CONF_GET_FLOAT("Transmogrification.SetCostModifier");
        cost += CONF_GET_INT("Transmogrification.SetCopperCost");

        if (!player->HasEnoughMoney(cost))
        {
            SendNotification(player, TRANSMOG_LOCALE_TRANSMOG_NOT_ENOUGH_MONEY);
            break;
        }

        std::ostringstream ss;

        for (auto const& itr : items)
        {
            ss << uint32(itr.first) << ' ' << itr.second << ' ';
            _presetById[player->GetGUID()][presetID][itr.first] = itr.second;
        }

        _presetByName[player->GetGUID()][presetID] = name; // Make sure code doesnt mess up SQL!
        CharacterDatabase.PExecute("REPLACE INTO `custom_transmogrification_sets` (`Owner`, `PresetID`, `SetName`, `SetData`) VALUES (%u, %u, \"%s\", \"%s\")", player->GetGUID().GetCounter(),
                                   uint32(presetID), name.c_str(), ss.str().c_str());

        if (cost)
            player->ModifyMoney(-cost);

        break;
    }
}

void Transmogrification::GossipShowTransmogItems(Player* player, Creature* creature, uint8 slot)
{
    if (!player || !creature)
        return;

    auto session = player->GetSession();

    Item* oldItem = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
    if (oldItem)
    {
        uint32 limit = 0;
        uint32 price = GetSpecialPrice(oldItem->GetTemplate());

        price *= CONF_GET_FLOAT("Transmogrification.ScaledCostModifier");
        price += CONF_GET_INT("Transmogrification.CopperCost");

        std::ostringstream ss;
        ss << std::endl;

        if (CONF_GET_BOOL("Transmogrification.RequireToken"))
            ss << std::endl << std::endl << CONF_GET_INT("Transmogrification.TokenAmount") << " x " << GetItemLink(CONF_GET_INT("Transmogrification.TokenEntry"), session);

        for (uint8 i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; ++i)
        {
            if (limit > MAX_OPTIONS)
                break;

            Item* newItem = player->GetItemByPos(INVENTORY_SLOT_BAG_0, i);
            if (!newItem)
                continue;

            if (!CanTransmogrifyItemWithItem(player, oldItem->GetTemplate(), newItem->GetTemplate()))
                continue;

            if (GetFakeEntry(oldItem->GetGUID()) == newItem->GetEntry())
                continue;

            ++limit;

            AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, GetItemIcon(newItem->GetEntry(), 30, 30, -18, 0) + GetItemLink(newItem, session), slot, newItem->GetGUID().GetCounter(),
                             GetGossipItemName(player, TRANSMOG_LOCALE_GOSSIP_ITEM_ITEM_BIND_Q) + GetItemIcon(newItem->GetEntry(), 40, 40, -15, -10) + GetItemLink(newItem, session) + ss.str(), price, false);
        }

        for (uint8 i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; ++i)
        {
            Bag* bag = player->GetBagByPos(i);
            if (!bag)
                continue;

            for (uint32 j = 0; j < bag->GetBagSize(); ++j)
            {
                if (limit > MAX_OPTIONS)
                    break;

                Item* newItem = player->GetItemByPos(i, j);
                if (!newItem)
                    continue;

                if (!CanTransmogrifyItemWithItem(player, oldItem->GetTemplate(), newItem->GetTemplate()))
                    continue;

                if (GetFakeEntry(oldItem->GetGUID()) == newItem->GetEntry())
                    continue;

                ++limit;

                AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, GetItemIcon(newItem->GetEntry(), 30, 30, -18, 0) + GetItemLink(newItem, session), slot, newItem->GetGUID().GetCounter(),
                                 GetGossipItemName(player, TRANSMOG_LOCALE_GOSSIP_ITEM_ITEM_BIND_Q) + GetItemIcon(newItem->GetEntry(), 40, 40, -15, -10) + GetItemLink(newItem, session) + ss.str(), price, false);
            }
        }
    }

    AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, GetGossipItemName(player, TRANSMOG_LOCALE_GOSSIP_ITEM_REMOVE_SINGLE_TRANSMOG), EQUIPMENT_SLOT_END + 3, slot,
                     GetGossipItemName(player, TRANSMOG_LOCALE_GOSSIP_ITEM_REMOVE_SINGLE_TRANSMOG_Q), 0, false);

    AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, GetGossipItemName(player, TRANSMOG_LOCALE_GOSSIP_ITEM_UPDATE_MENU), EQUIPMENT_SLOT_END, slot);
    AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, GetGossipItemName(player, TRANSMOG_LOCALE_GOSSIP_ITEM_BACK), EQUIPMENT_SLOT_END + 1, 0);

    SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
}

void Transmogrification::GossipRemoveAllTransmogrifications(Player* player)
{
    bool removed = false;

    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        if (Item* newItem = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
        {
            if (!GetFakeEntry(newItem->GetGUID()))
                continue;

            DeleteFakeEntry(player, slot, newItem);
            removed = true;
        }
    }

    SendNotification(player, removed ? TRANSMOG_LOCALE_UNTRANSMOG_OK : TRANSMOG_LOCALE_UNTRANSMOG_NO_TRANSMOGS);
}

void Transmogrification::GossipRemoveSingleTransmogrifications(Player* player, uint32 const& action)
{
    Item* newItem = player->GetItemByPos(INVENTORY_SLOT_BAG_0, action);
    if (!newItem)
        return;

    if (GetFakeEntry(newItem->GetGUID()))
    {
        DeleteFakeEntry(player, action, newItem);
        SendNotification(player, TRANSMOG_LOCALE_UNTRANSMOG_OK);
    }
    else
        SendNotification(player, TRANSMOG_LOCALE_UNTRANSMOG_NO_TRANSMOGS);
}

void Transmogrification::GossipShowPresetsMenu(Player* player, Creature* creature)
{
    if (CONF_GET_BOOL("Transmogrification.EnableSetInfo"))
        AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, GetGossipItemName(player, TRANSMOG_LOCALE_GOSSIP_ITEM_HOW_SET_WORKS), EQUIPMENT_SLOT_END + 10, 0);

    for (auto const& itr : _presetByName[player->GetGUID()])
        AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, "|TInterface/ICONS/INV_Misc_Statue_02:30:30:-18:0|t" + itr.second, EQUIPMENT_SLOT_END + 6, itr.first);

    if (static_cast<uint32>(_presetByName[player->GetGUID()].size()) < CONF_GET_UINT("Transmogrification.MaxSets"))
        AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, GetGossipItemName(player, TRANSMOG_LOCALE_GOSSIP_ITEM_SAVE_SET), EQUIPMENT_SLOT_END + 8, 0);

    AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, GetGossipItemName(player, TRANSMOG_LOCALE_GOSSIP_ITEM_BACK), EQUIPMENT_SLOT_END + 1, 0);

    SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
}

void Transmogrification::GossipUsePreset(Player* player, uint32 const& action)
{
    // action = presetID
    for (auto const& itr : _presetById[player->GetGUID()][action])
    {
        if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, itr.first))
            PresetTransmog(player, item, itr.second, itr.first);
    }
}

void Transmogrification::GossipViewPreset(Player* player, Creature* creature, uint32 const& action, uint32 const& sender)
{
    auto session = player->GetSession();

    // action = presetID
    for (auto const& itr : _presetById[player->GetGUID()][action])
        AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, GetItemIcon(itr.second, 30, 30, -18, 0) + GetItemLink(itr.second, session), sender, action);

    AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, GetGossipItemName(player, TRANSMOG_LOCALE_GOSSIP_ITEM_SET_USE), EQUIPMENT_SLOT_END + 5, action,
                     GetGossipItemName(player, TRANSMOG_LOCALE_GOSSIP_ITEM_ITEM_BIND_Q) + _presetByName[player->GetGUID()][action], 0, false);

    AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, GetGossipItemName(player, TRANSMOG_LOCALE_GOSSIP_ITEM_SET_DELETE), EQUIPMENT_SLOT_END + 7, action,
                     GetGossipItemName(player, TRANSMOG_LOCALE_GOSSIP_ITEM_SET_DELETE_Q) + _presetByName[player->GetGUID()][action] + "?", 0, false);

    AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, GetGossipItemName(player, TRANSMOG_LOCALE_GOSSIP_ITEM_BACK), EQUIPMENT_SLOT_END + 4, 0);
    SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
}

void Transmogrification::GossipDeletePreset(Player* player, uint32 const& action)
{
    // action = presetID
    CharacterDatabase.PExecute("DELETE FROM `custom_transmogrification_sets` WHERE Owner = %u AND PresetID = %u", player->GetGUID().GetCounter(), action);
    _presetById[player->GetGUID()][action].clear();
    _presetById[player->GetGUID()].erase(action);
    _presetByName[player->GetGUID()].erase(action);
}

void Transmogrification::GossipSavePreset(Player* player, Creature* creature, uint32 const& action, uint32 const& sender)
{
    uint32 cost = 0;
    bool canSave = false;
    auto session = player->GetSession();

    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        if (GetSlotName(player, slot).empty())
            continue;

        if (Item* newItem = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
        {
            uint32 entry = GetFakeEntry(newItem->GetGUID());
            if (!entry)
                continue;

            const ItemTemplate* temp = sObjectMgr->GetItemTemplate(entry);
            if (!temp)
                continue;

            if (!SuitableForTransmogrification(player, temp)) // no need to check?
                continue;

            cost += GetSpecialPrice(temp);
            canSave = true;

            AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, GetItemIcon(entry, 30, 30, -18, 0) + GetItemLink(entry, session), EQUIPMENT_SLOT_END + 8, 0);
        }
    }

    if (canSave)
        AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, GetGossipItemName(player, TRANSMOG_LOCALE_GOSSIP_ITEM_SAVE_SET), 0, 0,
                         GetGossipItemName(player, TRANSMOG_LOCALE_GOSSIP_ITEM_SET_INSERT_NAME), cost * CONF_GET_FLOAT("Transmogrification.SetCostModifier") + CONF_GET_INT("Transmogrification.SetCopperCost"), true);

    AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, GetGossipItemName(player, TRANSMOG_LOCALE_GOSSIP_ITEM_UPDATE_MENU), sender, action);
    AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, GetGossipItemName(player, TRANSMOG_LOCALE_GOSSIP_ITEM_BACK), EQUIPMENT_SLOT_END + 4, 0);
    SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
}

void Transmogrification::GossipTransmogrify(Player* player, uint32 const& action, uint32 const& sender)
{
    // sender = slot, action = display
    Transmogrify(player, ObjectGuid(HighGuid::Item, 0, action), sender);
}

void Transmogrification::SendNotification(Player* player, uint8 stringID)
{
    auto session = player->GetSession();
    if (!session)
        return;

    uint8 localeIndex = static_cast<uint8>(session->GetSessionDbLocaleIndex());

    session->SendNotification("%s", sModuleLocale->GetModuleString(MODULE_NAME, stringID, localeIndex).value().c_str());
}

void Transmogrification::OnGossipHello(Player* player, Creature* creature)
{
    if (CONF_GET_BOOL("Transmogrification.EnableTransmogInfo"))
        AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, GetGossipItemName(player, TRANSMOG_LOCALE_GOSSIP_ITEM_HOW_WORKS), EQUIPMENT_SLOT_END + 9, 0);

    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        if (!CanTransmogSlot(slot))
            continue;

        auto const& gossipIcon = GetGossipIcon(slot, player);
        if (gossipIcon.empty())
            continue;

        AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, gossipIcon, EQUIPMENT_SLOT_END, slot);
    }

    if (CONF_GET_BOOL("Transmogrification.EnableSets"))
        AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, GetGossipItemName(player, TRANSMOG_LOCALE_GOSSIP_ITEM_MANAGE_SETS), EQUIPMENT_SLOT_END + 4, 0);

    AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, GetGossipItemName(player, TRANSMOG_LOCALE_GOSSIP_ITEM_REMOVE_ALL_TRANSMOG), EQUIPMENT_SLOT_END + 2, 0,
                     GetGossipItemName(player, TRANSMOG_LOCALE_GOSSIP_ITEM_REMOVE_ALL_TRANSMOG_Q), 0, false);

    AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, GetGossipItemName(player, TRANSMOG_LOCALE_GOSSIP_ITEM_UPDATE_MENU), EQUIPMENT_SLOT_END + 1, 0);

    SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
}

void Transmogrification::OnGossipSelect(Player* player, Creature* creature, uint32 const& action, uint32 const& sender)
{
    ClearGossipMenuFor(player);

    switch (sender)
    {
    case EQUIPMENT_SLOT_END: // Show items you can use
        GossipShowTransmogItems(player, creature, action);
        break;
    case EQUIPMENT_SLOT_END + 1: // Main menu
        OnGossipHello(player, creature);
        break;
    case EQUIPMENT_SLOT_END + 2: // Remove Transmogrifications
        GossipRemoveAllTransmogrifications(player);
        OnGossipHello(player, creature);
        break;
    case EQUIPMENT_SLOT_END + 3: // Remove Transmogrification from single item
        GossipRemoveSingleTransmogrifications(player, action);
        OnGossipSelect(player, creature, EQUIPMENT_SLOT_END, action);
        break;
    case EQUIPMENT_SLOT_END + 4: // Presets menu
    {
        if (!CONF_GET_BOOL("Transmogrification.EnableSets"))
        {
            OnGossipHello(player, creature);
            return;
        }

        GossipShowPresetsMenu(player, creature);
    }
    break;
    case EQUIPMENT_SLOT_END + 5: // Use preset
    {
        if (!CONF_GET_BOOL("Transmogrification.EnableSets"))
        {
            OnGossipHello(player, creature);
            return;
        }

        GossipUsePreset(player, action);
        OnGossipSelect(player, creature, EQUIPMENT_SLOT_END + 6, action);
    }
    break;
    case EQUIPMENT_SLOT_END + 6: // view preset
    {
        if (!CONF_GET_BOOL("Transmogrification.EnableSets"))
        {
            OnGossipHello(player, creature);
            return;
        }

        GossipViewPreset(player, creature, action, sender);
    }
    break;
    case EQUIPMENT_SLOT_END + 7: // Delete preset
    {
        if (!CONF_GET_BOOL("Transmogrification.EnableSets"))
        {
            OnGossipHello(player, creature);
            return;
        }

        GossipDeletePreset(player, action);
        OnGossipSelect(player, creature, EQUIPMENT_SLOT_END + 4, 0);
    }
    break;
    case EQUIPMENT_SLOT_END + 8: // Save preset
    {
        if (!CanSavePresets(player))
        {
            OnGossipHello(player, creature);
            return;
        }

        GossipSavePreset(player, creature, action, sender);
    }
    break;
    case EQUIPMENT_SLOT_END + 10: // Set info
        AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, GetGossipItemName(player, TRANSMOG_LOCALE_GOSSIP_ITEM_BACK), EQUIPMENT_SLOT_END + 4, 0);
        SendGossipMenuFor(player, CONF_GET_INT("Transmogrification.SetNpcText"), creature->GetGUID());
        break;
    case EQUIPMENT_SLOT_END + 9: // Transmog info
        AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, GetGossipItemName(player, TRANSMOG_LOCALE_GOSSIP_ITEM_BACK), EQUIPMENT_SLOT_END + 1, 0);
        SendGossipMenuFor(player, CONF_GET_INT("Transmogrification.TransmogNpcText"), creature->GetGUID());
        break;
    default: // Transmogrify
    {
        if (!sender && !action)
        {
            OnGossipHello(player, creature);
            return;
        }

        GossipTransmogrify(player, action, sender);
        CloseGossipMenuFor(player); // Wait for SetMoney to get fixed, issue #10053
    }
    break;
    }
}

void Transmogrification::OnGossipSelectCode(Player* player, Creature* creature, uint32 const& action, uint32 const& sender, const char* code)
{
    ClearGossipMenuFor(player);

    if (sender || action)
        return; // should never happen

    if (!CONF_GET_BOOL("Transmogrification.EnableSets"))
    {
        OnGossipHello(player, creature);
        return;
    }

    SavePreset(player, std::string(code));

    CloseGossipMenuFor(player); // Wait for SetMoney to get fixed, issue #10053
}

void Transmogrification::MirrorImageDisplayItem(const Item* item, uint32& display)
{
    if (uint32 entry = GetFakeEntry(item->GetGUID()))
        display = uint32(sObjectMgr->GetItemTemplate(entry)->DisplayInfoID);
}

void Transmogrification::SetVisibleItemSlot(Player* player, uint8 slot, Item* item)
{
    if (!item)
        return;

    if (uint32 entry = GetFakeEntry(item->GetGUID()))
        player->SetUInt32Value(PLAYER_VISIBLE_ITEM_1_ENTRYID + (slot * 2), entry);
}
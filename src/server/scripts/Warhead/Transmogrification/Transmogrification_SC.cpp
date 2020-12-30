/*
6.1
Transmogrification 3.3.5a - Gossip menu
By Rochet2

ScriptName for NPC:
Creature_Transmogrify

TODO:
Fix the cost formula
-- Too much data handling, use default costs

Cant transmogrify rediculus items // Foereaper: would be fun to stab people with a fish
-- Cant think of any good way to handle this easily, could rip flagged items from cata DB
*/

#include "Transmogrification.h"
#include "Bag.h"
#include "Common.h"
#include "Config.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "DBCStructure.h"
#include "Define.h"
#include "Field.h"
#include "GameEventMgr.h"
#include "GossipDef.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Language.h"
#include "Log.h"
#include "Player.h"
#include "ObjectGuid.h"
#include "ObjectMgr.h"
#include "QueryResult.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "Transaction.h"
#include "WorldSession.h"
#include <sstream>
#include <string>

class Transmogrification_NPC : public CreatureScript
{
public:
    Transmogrification_NPC() : CreatureScript("Transmogrification_NPC") { }

    class TransmogAI : public ScriptedAI
    {
    public:
        TransmogAI(Creature* creature) : ScriptedAI(creature) {}

        bool OnGossipHello(Player* player) override
        {
            sTransmog->OnGossipHello(player, me);
            return true;
        }

        bool OnGossipSelect(Player* player, uint32 /*menu_id*/, uint32 gossipListId) override
        {
            sTransmog->OnGossipSelect(player, me, player->PlayerTalkClass->GetGossipOptionAction(gossipListId), player->PlayerTalkClass->GetGossipOptionSender(gossipListId));
            return true;
        }

        bool OnGossipSelectCode(Player* player, uint32 /*menu_id*/, uint32 gossipListId, const char* code) override
        {
            sTransmog->OnGossipSelectCode(player, me, player->PlayerTalkClass->GetGossipOptionAction(gossipListId), player->PlayerTalkClass->GetGossipOptionSender(gossipListId), code);
            return true;
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new TransmogAI(creature);
    }
};

class Transmogrification_Item : public ItemScript
{
public:
    Transmogrification_Item() : ItemScript("Transmogrification_Item") { }

    void OnItemDelFromDB(CharacterDatabaseTransaction /*trans*/, ObjectGuid::LowType itemGuid) override
    {
        sTransmog->DeleteFakeFromDB(itemGuid);
    }

    void OnMirrorImageDisplayItem(const Item* item, uint32& display) override
    {
        sTransmog->MirrorImageDisplayItem(item, display);
    }
};

class Transmogrification_Player : public PlayerScript
{
public:
    Transmogrification_Player() : PlayerScript("Transmogrification_Player") { }

    void OnPlayerAfterSetVisibleItemSlot(Player* player, uint8 slot, Item* item) override
    {
        sTransmog->SetVisibleItemSlot(player, slot, item);
    }

    void OnLogin(Player* player, bool /*firstLogin*/) override
    {
        sTransmog->LoadPlayerSets(player);
    }

    void OnDelete(ObjectGuid guid, uint32 /*accountId*/) override
    {
        sTransmog->OnPlayerDelete(guid);
    }
};

class Transmogrification_World : public WorldScript
{
public:
    Transmogrification_World() : WorldScript("Transmogrification_World") { }

    void OnConfigLoad(bool reload) override
    {
        sTransmog->LoadConfig(reload);
    }

    void OnStartup() override
    {
        sTransmog->LoadConfig(false);
    }
};

void AddSC_Transmogrification()
{
    new Transmogrification_Item();
    new Transmogrification_NPC();
    new Transmogrification_Player();
    new Transmogrification_World();
}

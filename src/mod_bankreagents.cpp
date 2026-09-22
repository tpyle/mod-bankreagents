/*
 * mod-bankreagents - move a recipe's reagents from the bank into the bags,
 * just in time, so the client is willing to send the craft.
 *
 * The server can already consume reagents straight out of the bank
 * (Crafting.AllowBankReagents in worldserver.conf). It turned out not to be
 * enough on its own, and finding out why cost a debug session: the client
 * runs its own reagent check against the bags *before sending the cast*, in
 * the executable rather than the UI. Several attempts to craft Bolt of Linen
 * Cloth with twenty Linen Cloth in the bank produced "Missing Reagent" on the
 * client and not a single Spell::prepare for spell 2963 in the server log,
 * against 2517 other player casts in the same window. Nothing server side and
 * nothing in an addon can talk the client out of that check. The reagents
 * have to be in the bags when the button is pressed.
 *
 * So the bags are topped up at the last possible moment and by the smallest
 * possible amount. The companion addon (client-patch/addon/BankReagents) tells
 * the server which recipe the player has just selected, through the addon
 * command channel the core already provides - an addon message with the
 * "AzerothCore" prefix and opcode 'i' is parsed by
 * AddonChannelCommandHandler::ParseCommands and run as a command from the
 * player, and never delivered as chat. This module is that command:
 *
 *     bankreagents pull <spellId>
 *
 * For each reagent of the recipe it moves stacks out of the bank until the
 * bags hold at least one full stack (or the recipe's own count, if larger),
 * using the same CanStoreItem -> RemoveItem -> StoreItem sequence as the
 * core's own bank window. One slot per reagent, so the bag-space dependency
 * the client imposes is a slot or two rather than the whole stock. The addon
 * repeats the request after every craft, which keeps Create All fed; and
 * because the server consumes from the bank for any shortfall, a craft that
 * outruns the top-up still succeeds.
 *
 * Security: SEC_PLAYER and Console::No, no target argument, and the only
 * effect is moving the caller's own items between the caller's own bank and
 * bags. The recipe must be one the player knows.
 */

#include "Chat.h"
#include "CommandScript.h"
#include "Config.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "WorldSession.h"

#include <algorithm>

using namespace Acore::ChatCommands;

namespace
{
    struct BankReagentsConfig
    {
        bool   Enable    = true;
        uint32 MinStacks = 1;   // full stacks per reagent to keep in the bags
    };

    BankReagentsConfig cfg;

    void LoadConfig()
    {
        cfg.Enable    = sConfigMgr->GetOption<bool>("BankReagents.Enable", true);
        cfg.MinStacks = std::max<uint32>(1, sConfigMgr->GetOption<uint32>("BankReagents.MinStacks", 1));
    }

    // Moves one item from a bank position into the bags, the way the bank
    // window does it. Returns how many units moved, or 0 if nothing fit.
    uint32 MoveToBags(Player* player, uint8 bag, uint8 slot)
    {
        Item* item = player->GetItemByPos(bag, slot);
        if (!item || item->IsInTrade())
            return 0;

        ItemPosCountVec dest;
        if (player->CanStoreItem(NULL_BAG, NULL_SLOT, dest, item, false) != EQUIP_ERR_OK)
            return 0;

        // StoreItem may merge the stack into an existing one and delete the
        // source object, so the count is taken before it moves.
        uint32 const count = item->GetCount();

        player->RemoveItem(bag, slot, true);
        if (Item const* stored = player->StoreItem(dest, item, true))
            player->ItemAddedQuestCheck(stored->GetEntry(), stored->GetCount());

        return count;
    }

    // Tops the bags up with `entry` from the bank until they hold at least
    // `wanted`, the bank runs out, or nothing more fits. Returns units moved.
    uint32 PullReagent(Player* player, uint32 entry, uint32 wanted)
    {
        uint32 moved = 0;

        auto const stillShort = [&]()
        {
            return player->GetItemCount(entry, false) < wanted;
        };

        if (!stillShort())
            return 0;

        for (uint8 i = BANK_SLOT_ITEM_START; i < BANK_SLOT_ITEM_END && stillShort(); ++i)
        {
            Item const* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, i);
            if (item && item->GetEntry() == entry)
            {
                uint32 const n = MoveToBags(player, INVENTORY_SLOT_BAG_0, i);
                if (!n)
                    return moved;   // no room in the bags: stop rather than churn
                moved += n;
            }
        }

        for (uint8 i = BANK_SLOT_BAG_START; i < BANK_SLOT_BAG_END && stillShort(); ++i)
        {
            Bag* bankBag = player->GetBagByPos(i);
            if (!bankBag)
                continue;

            for (uint32 j = 0; j < bankBag->GetBagSize() && stillShort(); ++j)
            {
                Item const* item = bankBag->GetItemByPos(j);
                if (item && item->GetEntry() == entry)
                {
                    uint32 const n = MoveToBags(player, i, j);
                    if (!n)
                        return moved;
                    moved += n;
                }
            }
        }

        return moved;
    }
}

class BankReagents_WorldScript : public WorldScript
{
public:
    BankReagents_WorldScript() : WorldScript("BankReagents_WorldScript",
        { WORLDHOOK_ON_AFTER_CONFIG_LOAD }) { }

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        LoadConfig();
    }
};

class BankReagents_CommandScript : public CommandScript
{
public:
    BankReagents_CommandScript() : CommandScript("BankReagents_CommandScript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable bankReagentsCommandTable =
        {
            { "pull", HandlePullCommand, SEC_PLAYER, Console::No }
        };

        static ChatCommandTable commandTable =
        {
            { "bankreagents", bankReagentsCommandTable }
        };

        return commandTable;
    }

    static bool HandlePullCommand(ChatHandler* handler, uint32 spellId)
    {
        if (!cfg.Enable)
            return false;

        Player* player = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
        if (!player)
            return false;

        SpellInfo const* spell = sSpellMgr->GetSpellInfo(spellId);
        if (!spell || !player->HasSpell(spellId))
        {
            handler->PSendSysMessage("bankreagents: not a recipe you know.");
            return false;
        }

        uint32 totalMoved = 0;
        for (uint8 i = 0; i < MAX_SPELL_REAGENTS; ++i)
        {
            int32 const entry = spell->Reagent[i];
            if (entry <= 0)
                continue;

            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(uint32(entry));
            if (!proto)
                continue;

            // Enough for the craft, and never less than the configured number
            // of full stacks, so a run of crafts does not need a round trip
            // for every single one.
            uint32 const wanted = std::max<uint32>(uint32(spell->ReagentCount[i]),
                                                   proto->GetMaxStackSize() * cfg.MinStacks);

            totalMoved += PullReagent(player, uint32(entry), wanted);
        }

        if (totalMoved)
            LOG_DEBUG("module", "mod-bankreagents: moved {} reagent unit(s) into {}'s bags for spell {}",
                totalMoved, player->GetName(), spellId);

        return true;
    }
};

void AddBankReagentsScripts()
{
    new BankReagents_WorldScript();
    new BankReagents_CommandScript();
}

/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
*/

#include "Chat.h"
#include "ScriptMgr.h"
#include "AuctionHouseBot.h"
#include "AuctionHouseBotProgressionProvider.h"
#include "Log.h"
#include "Mail.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "WorldSession.h"

class AHBot_WorldScript : public WorldScript
{
private:
    bool HasPerformedStartup;

public:
    AHBot_WorldScript() : WorldScript("AHBot_WorldScript"), HasPerformedStartup(false) { }

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        if (!auctionbot->IsModuleEnabled())
            return;

        auctionbot->InitializeConfiguration();
        if (HasPerformedStartup == true)
        {
            LOG_INFO("server.loading", "AuctionHouseBot: (Re)populating item candidate lists ...");
            auctionbot->PopulateItemCandidatesAndProportions();

            if (sConfigMgr->GetOption<bool>("AuctionHouseBot.AdvancedListingRules.UseDropRates.Enabled", false))
            {
                auctionbot->PopulateQuestRewardItemIDs();
                auctionbot->PopulateItemDropChances();
            }
        }
    }

    void OnStartup() override
    {
        if (!auctionbot->IsModuleEnabled())
            return;

        auctionbot->LoadSourceRarityPricing();

        AuctionHouseBotProgression::Policy progressionPolicy;

        if (AuctionHouseBotProgression::HasPolicyProvider() &&
            !AuctionHouseBotProgression::TryGetPolicy(
                progressionPolicy))
        {
            LOG_WARN(
                "module",
                "[AHBotPhase] Authoritative progression provider "
                "registered but policy is not ready during startup. "
                "Initial candidate-pool build deferred.");

            HasPerformedStartup = true;
            return;
        }

        LOG_INFO("server.loading", "AuctionHouseBot: (Re)populating item candidate lists ...");
        auctionbot->PopulateItemCandidatesAndProportions();
        if (sConfigMgr->GetOption<bool>("AuctionHouseBot.AdvancedListingRules.UseDropRates.Enabled", false))
        {
            auctionbot->PopulateQuestRewardItemIDs();
            auctionbot->PopulateItemDropChances();
        }
        HasPerformedStartup = true;
    }
};

class AHBot_AuctionHouseScript : public AuctionHouseScript
{
private:
    uint8 LastKnownPhase;
    bool AuthorityUnavailableLogged;

public:
    AHBot_AuctionHouseScript() :
        AuctionHouseScript("AHBot_AuctionHouseScript"),
        LastKnownPhase(0),
        AuthorityUnavailableLogged(false) { }

    void OnBeforeAuctionHouseMgrSendAuctionSuccessfulMail(AuctionHouseMgr* /*auctionHouseMgr*/, AuctionEntry* /*auction*/, Player* owner, uint32& /*owner_accId*/, uint32& /*profit*/, bool& sendNotification, bool& updateAchievementCriteria, bool& /*sendMail*/) override
    {
        if (owner)
        {
            bool isAHBot = false;
            for (AuctionHouseBotCharacter character : auctionbot->AHCharacters)
            {
                if (character.CharacterGUID == owner->GetGUID().GetCounter())
                {
                    isAHBot = true;
                    break;
                }
            }
            if (isAHBot == true)
            {
                sendNotification = false;
                updateAchievementCriteria = false;
            }
        }
    }

    void OnBeforeAuctionHouseMgrSendAuctionExpiredMail(AuctionHouseMgr* /*auctionHouseMgr*/, AuctionEntry* /*auction*/, Player* owner, uint32& /*owner_accId*/, bool& sendNotification, bool& sendMail) override
    {
        if (owner)
        {
            bool isAHBot = false;
            for (AuctionHouseBotCharacter character : auctionbot->AHCharacters)
            {
                if (character.CharacterGUID == owner->GetGUID().GetCounter())
                {
                    isAHBot = true;
                    break;
                }
            }
            if (isAHBot == true)
            {
                sendNotification = false;

                if (sConfigMgr->GetOption<bool>("AuctionHouseBot.ReturnExpiredAuctionItemsToBot", false))
                    sendMail = true;
                else
                    sendMail = false;
            }
        }   
    }

    void OnBeforeAuctionHouseMgrSendAuctionOutbiddedMail(AuctionHouseMgr* /*auctionHouseMgr*/, AuctionEntry* auction, Player* oldBidder, uint32& /*oldBidder_accId*/, Player* newBidder, uint32& newPrice, bool& /*sendNotification*/, bool& /*sendMail*/) override
    {
        if (oldBidder && !newBidder)
            oldBidder->GetSession()->SendAuctionBidderNotification((uint32)auction->GetHouseId(), auction->Id, ObjectGuid::Create<HighGuid::Player>(auctionbot->CurrentBotCharGUID), newPrice, auction->GetAuctionOutBid(), auction->item_template);
    }

    void OnBeforeAuctionHouseMgrSendAuctionWonMail(AuctionHouseMgr* /*auctionHouseMgr*/, AuctionEntry* /*auction*/, Player* bidder, uint32& /*bidder_accId*/, bool& sendNotification, bool& updateAchievementCriteria, bool& /*sendMail*/) override
    {
        // The bot buyer is a shell Player that never went through a full load (no map, no achievement data),
        // so suppress the paths that would touch that missing state when it wins an auction
        if (bidder)
        {
            bool isAHBot = false;
            for (AuctionHouseBotCharacter character : auctionbot->AHCharacters)
            {
                if (character.CharacterGUID == bidder->GetGUID().GetCounter())
                {
                    isAHBot = true;
                    break;
                }
            }
            if (isAHBot == true)
            {
                sendNotification = false;
                updateAchievementCriteria = false;
            }
        }
    }

    void OnBeforeAuctionHouseMgrUpdate() override
    {
        /*
         * Synchronize the AHBot market with the external
         * authoritative progression policy.
         */
        AuctionHouseBotProgression::Policy phase;

        if (!AuctionHouseBotProgression::TryGetPolicy(
                phase))
        {
            if (AuctionHouseBotProgression::
                    HasPolicyProvider())
            {
                if (!AuthorityUnavailableLogged)
                {
                    LOG_WARN(
                        "module",
                        "[AHBotPhase] Authoritative progression "
                        "provider registered but policy unavailable. "
                        "AHBot market update deferred.");

                    AuthorityUnavailableLogged = true;
                }

                return;
            }

            /*
             * No external authority registered:
             * preserve standalone AHBot behavior.
             */
            AuthorityUnavailableLogged = false;
            auctionbot->Update();
            return;
        }

        AuthorityUnavailableLogged = false;

        {
            uint8 currentPhase = phase.phase;

            if (LastKnownPhase == 0)
            {
                /*
                 * Rebuild once on first authoritative policy.
                 * This also covers startup-order cases where
                 * AHBot built its initial pool before the
                 * progression authority became available.
                 *
                 * Existing auctions are not emptied here.
                 */
                LastKnownPhase = currentPhase;

                auctionbot->
                    PopulateItemCandidatesAndProportions();

                LOG_INFO(
                    "module",
                    "[AHBotPhase] Initial authoritative "
                    "policy detected. Candidate pool "
                    "synchronized with phase {}.",
                    static_cast<uint32>(
                        currentPhase));
            }
            else if (currentPhase != LastKnownPhase)
            {
                LOG_INFO(
                    "module",
                    "[AHBotPhase] Phase changed {} -> {}. "
                    "Rebuilding AHBot market. "
                    "PlayerMaxLevel={}, "
                    "MaterialMaxItemLevel={}.",
                    static_cast<uint32>(
                        LastKnownPhase),
                    static_cast<uint32>(
                        currentPhase),
                    static_cast<uint32>(
                        phase.maxPlayerLevel),
                    phase.auctionMaterialMaxItemLevel);

                auctionbot->EmptyAuctionHouses();
                auctionbot->CleanupExpiredAuctionItems();

                auctionbot->
                    PopulateItemCandidatesAndProportions();

                if (sConfigMgr->GetOption<bool>(
                        "AuctionHouseBot.AdvancedListingRules.UseDropRates.Enabled",
                        false))
                {
                    auctionbot->PopulateQuestRewardItemIDs();
                    auctionbot->PopulateItemDropChances();
                }

                LastKnownPhase = currentPhase;

                LOG_INFO(
                    "module",
                    "[AHBotPhase] Market synchronized "
                    "with phase {}.",
                    static_cast<uint32>(
                        currentPhase));
            }
        }

        auctionbot->Update();
    }
};

class AHBot_MailScript : public MailScript
{
public:
    AHBot_MailScript() : MailScript("AHBot_MailScript") { }

    void OnBeforeMailDraftSendMailTo(MailDraft* /*mailDraft*/, MailReceiver const& receiver, MailSender const& sender, MailCheckMask& /*checked*/, uint32& /*deliver_delay*/, uint32& /*custom_expiration*/, bool& deleteMailItemsFromDB, bool& sendMail) override
    {
        bool isAHBot = false;
        for (AuctionHouseBotCharacter character : auctionbot->AHCharacters)
        {
            if (character.CharacterGUID == receiver.GetPlayerGUIDLow())
            {
                isAHBot = true;
                break;
            }
        }
        if (isAHBot == true)
        {
            if (sConfigMgr->GetOption<bool>("AuctionHouseBot.ReturnExpiredAuctionItemsToBot", false))
            {
                deleteMailItemsFromDB = false;
                sendMail = true;
            }
            else
            {
                if (sender.GetMailMessageType() == MAIL_AUCTION)        // auction mail with items
                    deleteMailItemsFromDB = true;
                sendMail = false;
            }
        }
    }
};

class AHBot_CommandScript : public CommandScript
{
public:
    AHBot_CommandScript() : CommandScript("AHBot_CommandScript") { }

    Acore::ChatCommands::ChatCommandTable GetCommands() const override
    {
        static Acore::ChatCommands::ChatCommandTable AHBotCommandTable = {
            {"update", HandleAHBotUpdateCommand, SEC_GAMEMASTER, Acore::ChatCommands::Console::Yes},
            {"reload", HandleAHBotReloadCommand, SEC_GAMEMASTER, Acore::ChatCommands::Console::Yes},
            {"empty",  HandleAHBotEmptyCommand,  SEC_GAMEMASTER, Acore::ChatCommands::Console::Yes},
            {"price",  HandleAHBotPriceCommand,  SEC_GAMEMASTER, Acore::ChatCommands::Console::Yes},
            {"help",   HandleAHBotHelpCommand,   SEC_GAMEMASTER, Acore::ChatCommands::Console::Yes}
        };

        static Acore::ChatCommands::ChatCommandTable commandTable = {
            {"ahbot", AHBotCommandTable},
        };

        return commandTable;
    }


    static bool HandleAHBotPriceCommand(
        ChatHandler* handler,
        char const* args)
    {
        if (!args || !*args)
        {
            handler->SendSysMessage(
                "Usage: .ahbot price <itemId>");

            return false;
        }

        uint32 itemId =
            static_cast<uint32>(
                std::strtoul(args, nullptr, 10));

        ItemTemplate const* item =
            sObjectMgr->GetItemTemplate(itemId);

        if (!item)
        {
            handler->PSendSysMessage(
                "AHBot: item {} not found.",
                itemId);

            return false;
        }

        uint64 bid = 0;
        uint64 buyout = 0;

        auctionbot->CalculateItemValue(
            item,
            bid,
            buyout);

        double dropRate =
            auctionbot->GetCachedItemDropRate(
                itemId);

        float rarity =
            auctionbot->
                GetRarityPricingMultiplier(item);

        float sourceRarity =
            auctionbot->
                GetSourceRarityPricingMultiplier(
                    itemId);

        float endgame =
            auctionbot->
                GetEndgamePricingMultiplier(item);

        float special =
            auctionbot->
                GetSpecialItemPriceMultiplier(
                    itemId);

        float phase =
            auctionbot->
                GetPhasePricingMultiplier();

        handler->PSendSysMessage(
            "=== AHBot price debug [{}] {} ===",
            itemId,
            item->Name1);

        handler->PSendSysMessage(
            "RequiredLevel={} ItemLevel={} Quality={} "
            "SellPrice={} copper",
            item->RequiredLevel,
            item->ItemLevel,
            item->Quality,
            item->SellPrice);

        handler->PSendSysMessage(
            "DropRate={}%% Rarity=x{} Source=x{} "
            "Endgame=x{} Special=x{} Phase=x{}",
            dropRate,
            rarity,
            sourceRarity,
            endgame,
            special,
            phase);

        handler->PSendSysMessage(
            "Calculated unit Bid={} copper "
            "Buyout={} copper",
            bid,
            buyout);

        return true;
    }

    static bool HandleAHBotUpdateCommand(ChatHandler* handler, const char* /*args*/)
    {
        LOG_INFO("module", "AuctionHouseBot: Updating Auction House...");
        handler->PSendSysMessage("AuctionHouseBot: Updating Auction House...");
        AuctionHouseBot::instance()->Update();
        LOG_INFO("module", "AuctionHouseBot: Auction House Updated.");
        handler->PSendSysMessage("AuctionHouseBot: Auction House Updated.");
        return true;
    }

    static bool HandleAHBotReloadCommand(ChatHandler* handler, char const* /*args*/)
    {
        LOG_INFO("module", "AuctionHouseBot: Reloading Config...");
        handler->PSendSysMessage("AuctionHouseBot: Reloading Config...");

        // Reload config file with isReload = true
        sConfigMgr->LoadModulesConfigs(true, false);
        AuctionHouseBot::instance()->InitializeConfiguration();
        AuctionHouseBot::instance()->LoadSourceRarityPricing();
        AuctionHouseBot::instance()->PopulateItemCandidatesAndProportions();

        if (sConfigMgr->GetOption<bool>("AuctionHouseBot.AdvancedListingRules.UseDropRates.Enabled", true))
        {
            auctionbot->PopulateQuestRewardItemIDs();
            auctionbot->PopulateItemDropChances();
        }

        LOG_INFO("module", "AuctionHouseBot: Config reloaded.");
        handler->PSendSysMessage("AuctionHouseBot: Config reloaded.");        
        return true;
    }

    static bool HandleAHBotEmptyCommand(ChatHandler* handler, char const* /*args*/)
    {
        LOG_INFO("module", "AuctionHouseBot: Emptying Auction House...");
        handler->PSendSysMessage("AuctionHouseBot: Emptying Auction House...");
        AuctionHouseBot::instance()->EmptyAuctionHouses();
        AuctionHouseBot::instance()->CleanupExpiredAuctionItems(); // Must go after EmptyAuctionHouses()
        LOG_INFO("module", "AuctionHouseBot: Auction Houses Emptied.");
        handler->PSendSysMessage("AuctionHouseBot: Auction Houses Emptied.");
        return true;
    }

    static bool HandleAHBotHelpCommand(ChatHandler* handler, char const* /*args*/)
    {
        handler->PSendSysMessage("AuctionHouseBot commands:");
        handler->PSendSysMessage("  .ahbot reload - Reloads configuration");
        handler->PSendSysMessage("  .ahbot empty  - Removes all AuctionHouseBot auctions");
        handler->PSendSysMessage("  .ahbot update - Runs an update cycle");
        return true;
    }
};

void AddAHBotScripts()
{
    new AHBot_WorldScript();
    new AHBot_AuctionHouseScript();
    new AHBot_MailScript();
    new AHBot_CommandScript();
}

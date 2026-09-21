#ifndef AUCTION_HOUSE_BOT_PROGRESSION_PROVIDER_H
#define AUCTION_HOUSE_BOT_PROGRESSION_PROVIDER_H

#include <cstdint>

namespace AuctionHouseBotProgression
{
    struct Policy
    {
        std::uint8_t phase = 20;
        std::uint8_t maxPlayerLevel = 20;

        std::uint32_t auctionMaterialMaxItemLevel = 20;

        std::uint16_t craftingCap = 150;
        std::uint16_t gatheringCap = 150;
        std::uint16_t secondaryCap = 150;
        std::uint16_t ridingCap = 75;
    };

    using PolicyProvider =
        bool (*)(Policy& policy);

    void RegisterPolicyProvider(
        PolicyProvider provider);

    bool HasPolicyProvider();

    bool TryGetPolicy(
        Policy& policy);

    std::uint16_t GetProfessionCap(
        Policy const& policy,
        std::uint32_t skillId);
}

#endif

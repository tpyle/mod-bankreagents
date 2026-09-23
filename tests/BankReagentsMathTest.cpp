/*
 * Tests for mod-bankreagents' top-up quantity (src/BankReagentsMath.h).
 */

#include "BankReagentsMath.h"

#include <gtest/gtest.h>
#include <limits>

using BankReagentsMath::SaneMinStacks;
using BankReagentsMath::WantedCount;

TEST(BankReagentsMinStacks, NeverPullsNothing)
{
    // Zero stacks would mean the bags stay empty and the client refuses the
    // craft - indistinguishable, to a player, from the module not working.
    EXPECT_EQ(SaneMinStacks(0), 1u);
    EXPECT_EQ(SaneMinStacks(1), 1u);
    EXPECT_EQ(SaneMinStacks(4), 4u);
}

TEST(BankReagentsWanted, PullsAFullStackForAnOrdinaryRecipe)
{
    // Bolt of Linen Cloth: two Linen Cloth per craft, stacking to 999 here.
    EXPECT_EQ(WantedCount(/*reagentCount*/ 2, /*maxStackSize*/ 999, /*minStacks*/ 1), 999u);
}

TEST(BankReagentsWanted, ARecipeThatNeedsMoreThanAStackGetsWhatItNeeds)
{
    EXPECT_EQ(WantedCount(/*reagentCount*/ 40, /*maxStackSize*/ 20, /*minStacks*/ 1), 40u);
}

TEST(BankReagentsWanted, MoreStacksMeansFewerTripsToTheBank)
{
    EXPECT_EQ(WantedCount(5, 20, 3), 60u);
}

TEST(BankReagentsWanted, ZeroStacksStillPullsEnoughToCraft)
{
    EXPECT_EQ(WantedCount(5, 20, 0), 20u);
}

TEST(BankReagentsWanted, AnUnstackableReagentIsPulledByTheRecipesCount)
{
    // maxStackSize 1 is a leatherworking pattern or a rod: one is one.
    EXPECT_EQ(WantedCount(1, 1, 1), 1u);
    EXPECT_EQ(WantedCount(3, 1, 1), 3u);
}

TEST(BankReagentsWanted, AbsurdConfigurationSaturatesRatherThanWrapping)
{
    // 999 * 40,000,000 does not fit in 32 bits. Wrapping would ask for a
    // small number and quietly pull too little; saturating asks for
    // everything, which PullReagent bounds by what the bank actually holds.
    constexpr auto MAX = std::numeric_limits<std::uint32_t>::max();

    EXPECT_EQ(WantedCount(2, 999, 40000000u), MAX);
    EXPECT_EQ(WantedCount(2, MAX, 2), MAX);
}

TEST(BankReagentsWanted, AZeroStackSizeLeavesTheRecipesCountInCharge)
{
    // GetMaxStackSize() should never be 0, but item data is data. A zero
    // stack size means there is no stack floor to impose, so the recipe's own
    // requirement governs - and, the reason the guard is written the way it
    // is, the overflow check never divides by it.
    EXPECT_EQ(WantedCount(7, 0, 5), 7u);
    EXPECT_EQ(WantedCount(0, 0, 5), 0u);
}

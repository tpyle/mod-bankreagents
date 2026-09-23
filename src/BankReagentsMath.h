/*
 * mod-bankreagents - how much of a reagent to pull out of the bank.
 *
 * Small, but worth stating once and testing: the number decides how often a
 * crafting run has to go back to the bank, and it multiplies two values that
 * both come from data (a recipe's reagent count, an item's stack size), so
 * it is exactly the kind of arithmetic that overflows quietly if someone
 * sets BankReagents.MinStacks to something silly.
 *
 * Standard library only, so tests/ builds without AzerothCore.
 */

#ifndef MOD_BANKREAGENTS_MATH_H
#define MOD_BANKREAGENTS_MATH_H

#include <algorithm>
#include <cstdint>
#include <limits>

namespace BankReagentsMath
{
    using u32 = std::uint32_t;

    // At least one stack is always kept, whatever the configuration says:
    // zero would mean pulling nothing and the craft failing on the client,
    // which looks like the module being broken rather than misconfigured.
    inline u32 SaneMinStacks(u32 configured)
    {
        return std::max<u32>(1, configured);
    }

    // Enough for this craft, and never less than the configured number of
    // full stacks so that a run of crafts does not need a round trip to the
    // bank for every single one.
    //
    // maxStackSize is the item's own limit, which on this realm is 999 for
    // trade goods - so the multiplication is the part that has to be guarded.
    inline u32 WantedCount(u32 reagentCount, u32 maxStackSize, u32 minStacks)
    {
        minStacks = SaneMinStacks(minStacks);

        u32 stacks = std::numeric_limits<u32>::max();
        if (maxStackSize == 0 || minStacks <= std::numeric_limits<u32>::max() / maxStackSize)
            stacks = maxStackSize * minStacks;

        return std::max(reagentCount, stacks);
    }
}

#endif // MOD_BANKREAGENTS_MATH_H

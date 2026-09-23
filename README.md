# mod-bankreagents

Moves a recipe's reagents from the bank into the bags, just in time, so the
client is willing to send the craft.

The server can consume reagents straight out of the bank on its own
(`Crafting.AllowBankReagents`, a core change). That turned out not to be
enough: the 3.3.5 client runs its own reagent check against the bags *in the
executable* before it sends the cast. Crafting Bolt of Linen Cloth with
twenty Linen Cloth in the bank produced "Missing Reagent" on the client and
not a single `Spell::prepare` on the server. Nothing server side and nothing
in an addon can talk the client out of that check, so the reagents have to be
in the bags when the button is pressed.

This module tops the bags up at the last possible moment and by the smallest
possible amount. The companion addon tells it which recipe was selected,
through the addon command channel the core already provides:

    bankreagents pull <spellId>

For each reagent it moves stacks out of the bank until the bags hold at least
one full stack (or the recipe's count, if larger), using the same
`CanStoreItem` -> `RemoveItem` -> `StoreItem` sequence as the bank window. One
slot per reagent, so the bag space it needs is a slot or two rather than the
whole stock.

`SEC_PLAYER`, `Console::No`, no target argument: the only effect is moving the
caller's own items between the caller's own bank and bags, and the recipe has
to be one the caller knows.

## Configuration (`mod_bankreagents.conf`)

| key | meaning |
| --- | --- |
| `BankReagents.Enable` | master switch |
| `BankReagents.MinStacks` | full stacks per reagent to keep in the bags (default 1) |

## Requirements

* A core with `Crafting.AllowBankReagents` (`Spell::CheckItems` /
  `Spell::TakeReagents` counting the bank, and `Player::DestroyItemCount`
  taking an `inBankAlso` flag).
* The `BankReagents` client addon, which sends the command on recipe
  selection and after each craft.

## Licence

GNU Affero General Public License v3.0, the licence AzerothCore and its
modules use. See [LICENSE](LICENSE).

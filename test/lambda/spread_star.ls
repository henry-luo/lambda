// Fixture of vibe/impl/Lambda_List_Fixes.md, green since P4 (2026-09-23) on
// both tiers. Golden written from the rulings, not from the runtime.
// S12.3.5v2: `*x` splices a sequence's items (list, array, range), nothing
// for null, a non-sequence as one item, and never modifies its operand —
// so [*xs] packages any value as an array.

let a = [1, 2]
fn lit() => [1, "y"]
"-- spreading does not modify its operand (pooled JIT literal included) --";
[*a, 3];
[a, 9];
[*lit(), 3];
[lit(), 9];
[len([*lit(), 3]), len([lit(), 9])]
"-- ranges, null, scalars, lists --";
[*(1 to 3), 9];
[*null, 9];
[*5, 9];
[*(1, 2), 9];
[*[*a]]
"-- pooled JIT literal: a later call must be unchanged (list-literal placement) --";
[len([(lit(), 9)])]
let spread_once = [*lit(), 3];
[len([(lit(), 9)]), len(spread_once)]
"-- the packager --";
[[*null], [*5], [*(5, 6)], [*[5, 6]], [*"ab"]]
let l = (5, 6);
[type([*l]), [*l] == l]

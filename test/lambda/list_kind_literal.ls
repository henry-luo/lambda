// P0 fixture of vibe/impl/Lambda_List_Fixes.md — lives in test/lambda/ext until its
// phase turns it green, then moves to test/lambda (baseline). Golden written from the
// rulings, not from the runtime.
// S2.5.1v2: a list is a specialized array that spreads where it sits as an item,
// never nests, and does not normalize its items. (phase P1)
// Green on both tiers after P1 (2026-09-22); moved from test/lambda/ext.

"-- kind --";
[type((1, 2)), type([1, 2]), type(1 to 3)];
[(1, 2) is list, (1, 2) is array, (1, 2) is int[], [1, 2] is list, (1 to 3) is list, (1 to 3) is array]
"-- spreads as an item; never nests --";
[(1, 2), 3];
[len(((1, 2), (3, 4))), ((1, 2), (3, 4)) == (1, 2, 3, 4)];
[[1, 2], 3];
[(1 to 3), 9]
"-- no normalization --";
[len((1, null, 2)), len(("a", "b")), type(("a", "b"))]
let l = (1, null, 2);
[l]
let s = ("a", "b");
[s]
"-- equality with arrays (S5.3.1) --";
[(1, 2) == [1, 2], (1, 2, 3) == (1 to 3)]

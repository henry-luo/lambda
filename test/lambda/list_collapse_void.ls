// P0 fixture of vibe/impl/Lambda_List_Fixes.md — lives in test/lambda/ext until its
// phase turns it green, then moves to test/lambda (baseline). Golden written from the
// rulings, not from the runtime.
// S2.5.5v2: a list has at least two items; (x) ≡ x and () ≡ null; a list
// EXPRESSION in item position splices what it has (void), a list VALUE has
// collapsed (a bound empty list is null and lands as null). S8.3.1v3: a
// scalar has no content, so a computed one-item list is invisible to for/len/
// index — wrap in [...] for a stable collection. (phase P1; take: P2)
// Green on both tiers after P2 (2026-09-22); moved from test/lambda/ext.

"-- collapse --";
[(5) == 5, type((5)), ((5)) == 5, (((1, 2))) == (1, 2)]
let z = { let k = 1; 7 };
[z, type(z)]
let b2 = { 1; 2 };
[b2]
let decl_only = { let q = 3 };
[decl_only == null, decl_only is null]
"-- an empty for-expression is null as a value --"
let e = for (x in []) x;
[e == null, e is null];
[e, 1];
[for (x in []) x, 1];
[[for (x in []) x], [e]]
"-- a one-item for-expression is its item --"
let one = for (x in [5]) x;
[one, type(one), one == 5];
[one, 1];
[for (x in [5]) x, 1];
[for (x in [5]) x]
"-- two items stay a list --"
let two = for (x in [5, 6]) x;
[type(two), two == (5, 6), two == [5, 6]];
[two, 1]
"-- the n = 1 edge (S8.3.1v3) --"
let t1 = take((10, 20, 30), 1)
let t2 = take((10, 20, 30), 2)
let t0 = take((10, 20, 30), 0);
[t1, len(t1), t1[0] == null, len([t1])];
[len(t2), t2[0], len([t2])];
[t0 == null, len(t0), len([t0])];
[for (v in t1) v];
[for (v in [t1]) v]
"-- collapse never drops an item --";
[len((1, null)), len((null, null))];
[(null, null)]

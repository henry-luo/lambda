// P0 fixture of vibe/impl/Lambda_List_Fixes.md — lives in test/lambda/ext until its
// phase turns it green, then moves to test/lambda (baseline). Golden written from the
// rulings, not from the runtime.
// S2.5.2v2: a for-expression produces a list whatever its clauses; nested
// for-results splice; empty and one-item results collapse (S2.5.5v2).
// (phase P2)
// Green on both tiers after P2 (2026-09-22); moved from test/lambda/ext.

let xs = [3, 1, 2]
"-- every clause keeps it a list --";
[type(for (x in xs) x), type(for (x in xs where x > 1) x)];
[type(for (x in xs order by x) x), type(for (x in xs order by x desc) x)];
[type(for (x in xs order by x limit 2) x), type(for (x in xs limit 2) x)];
[type(for (r in [{k: 1}, {k: 0}, {k: 1}] group by r.k into g) len(content(g)))];
[for (x in xs order by x) x, 9];
[for (x in xs order by x desc) x, 9];
[for (x in xs order by x limit 2) x, 9];
[for (x in xs order by x offset 1) x, 9];
[for (x in xs limit 1) x, 9]
"-- nested for: the inner list splices into the outer --";
[for (x in [1, 2]) for (y in [10, 20]) x + y];
[for (x in [1, 2]) (x, x)];
[for (x in [1, 2]) [x, x]]
"-- empty and one-item results collapse --"
let none = for (x in xs where x > 5) x
let just = for (x in xs where x > 2) x;
[none == null, just, type(just)]

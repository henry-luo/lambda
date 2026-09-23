// P0 fixture of vibe/impl/Lambda_List_Fixes (done).md — lives in test/lambda/ext until its
// phase turns it green, then moves to test/lambda (baseline). Golden written from the
// rulings, not from the runtime.
// S10.1.2v2 / S10.1.5v2: the mapping pipe and `that` keep the source's kind;
// a non-sequence scalar is one member; an empty result is null for a list or
// scalar source and [] for an array, range, or map source; a computed
// one-item list collapses. (phase P2)
// Green on both tiers after P2 (2026-09-22); moved from test/lambda/ext.

let L = (1, 2, 3)
let A = [1, 2, 3]
"-- mapping pipe --";
[type(L |> ~ * 2), type(A |> ~ * 2), type((1 to 3) |> ~ * 2)];
[L |> ~ * 2, 9];
[A |> ~ * 2, 9];
[5 |> ~ + 1, type(5 |> ~ + 1)];
[type({a: 1, b: 2} |> ~ * 10), {a: 1, b: 2} |> ~ * 10];
[null |> ~ + 1]
"-- filter --";
[type(L that ~ > 1), type(A that ~ > 1)];
[L that ~ > 1, 9];
[A that ~ > 1, 9];
[L that ~ > 9, 9];
[(L that ~ > 9) == null, A that ~ > 9, len(A that ~ > 9)];
[5 that ~ > 3, (5 that ~ > 9) == null];
[type({a: 1, b: 2} that ~ > 1), {a: 1, b: 2} that ~ > 1];
[type((1 to 3) that ~ > 1), (1 to 3) that ~ > 1]
"-- a one-item result collapses --"
let one = L that ~ > 2;
[one, type(one)];
[L that ~ > 2, 9]
"-- whole-value application is unchanged --";
[A |> sum, L |> len]

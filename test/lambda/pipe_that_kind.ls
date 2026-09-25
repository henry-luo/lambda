// P0 fixture of vibe/impl/Lambda_List_Fixes (done).md — lives in test/lambda/ext until its
// phase turns it green, then moves to test/lambda (baseline). Golden written from the
// rulings, not from the runtime.
// S10.1.2v4 / S10.1.6 / S2.5.7v2: the mapping pipe and the `|:` filter return an
// array for every sequence source, a list included; a non-sequence scalar is
// one member; an empty result is [] for a sequence source and null for a
// scalar source; a one-item result never collapses. (phase P2; list results
// flipped to arrays by S2.5.7v2, 2026-09-25)
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
[type(L |: ~ > 1), type(A |: ~ > 1)];
[L |: ~ > 1, 9];
[A |: ~ > 1, 9];
[L |: ~ > 9, 9];
[(L |: ~ > 9) == null, A |: ~ > 9, len(A |: ~ > 9)];
[5 |: ~ > 3, (5 |: ~ > 9) == null];
[type({a: 1, b: 2} |: ~ > 1), {a: 1, b: 2} |: ~ > 1];
[type((1 to 3) |: ~ > 1), (1 to 3) |: ~ > 1]
"-- a one-item result stays an array --"
let one = L |: ~ > 2;
[one, type(one)];
[L |: ~ > 2, 9]
"-- whole-value application is unchanged --";
[A |> sum, L |> len]

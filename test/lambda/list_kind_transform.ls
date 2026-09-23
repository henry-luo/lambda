// P0 fixture of vibe/impl/Lambda_List_Fixes.md — lives in test/lambda/ext until its
// phase turns it green, then moves to test/lambda (baseline). Golden written from the
// rulings, not from the runtime.
// S2.5.7: sequence operations preserve the input kind; mixing gives an array;
// constructors fix their own kind; fill follows its item; zip pairs are arrays.
// (phase P2)
// Green on both tiers after P2 (2026-09-22); moved from test/lambda/ext.

let L = (3, 1, 2)
let A = [3, 1, 2]
let R = 1 to 3
"-- reorder / select --";
[type(sort(L)), type(sort(A)), type(sort(R))];
[type(reverse(L)), type(reverse(A)), type(reverse(R))];
[type(unique((1, 1, 2))), type(unique([1, 1, 2]))];
[type(take(L, 2)), type(take(A, 2)), type(take(R, 2))];
[type(drop(L, 1)), type(drop(A, 1)), type(drop(R, 1))];
[type(slice(L, 0, 2)), type(slice(A, 0, 2)), type(slice(R, 0, 2))];
[type(L[0 to 1]), type(A[0 to 1]), type(R[0 to 1])];
[sort(L), 9];
[sort(A), 9];
[reverse(R), 9]
"-- vector arithmetic --";
[type(L * 2), type(A * 2), type(R * 2)];
[type(L + A), type(L + (1, 1, 1)), type(L + 1)];
[L * 2, 9];
[L + A, 9]
"-- masks and set operators --";
[type(L eq 1), type(A eq 1)];
[type(L | (2, 5)), type(L | [2, 5]), type(A | A)]
"-- zip --";
[type(zip(L, L)), type(zip(L, A)), type(zip(A, A))];
[len(zip(L, L)), type(zip(L, L)[0]), len(zip(L, L)[0])];
[zip((1, 2), (3, 4)), 9]
"-- fill --";
[type(fill(2, (1, 2))), len(fill(2, (1, 2))), type(fill(2, [1, 2])), len(fill(2, [1, 2])), type(fill(2, 5))];
[fill(2, (1, 2)), 9]
"-- constructors --";
[type(split("a,b", ",")), type(range(0, 3, 1)), type(content(<e 1 2>))];
[split("a,b", ","), 9]
"-- for always builds a list --";
[type(for (x in A) x), type(for (x in L) x), type(for (x in R) x)];
[for (x in A) x * 2, 9]

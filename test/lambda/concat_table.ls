// Fixture of vibe/impl/Lambda_List_Fixes (done).md, green since P4 (2026-09-23) on
// both tiers. Golden written from the rulings, not from the runtime.
// S10.6.1: `++` concatenates sequences (kind by S2.5.7), appends or prepends a
// scalar as one item, concatenates text for two scalars, treats null as the
// identity, and rejects maps. LR05-9: numeric arrays of different element
// types concatenate by value.

let L = (1, 2)
let A = [1, 2]
"-- sequence ++ sequence: kind by S2.5.7 --";
[type(L ++ (3, 4)), type(L ++ A), type(A ++ L), type(A ++ A), type(A ++ (1 to 2)), type(L ++ (1 to 2))];
[L ++ (3, 4), 9];
[A ++ L, 9];
[L ++ A, 9]
"-- sequence ++ scalar appends; scalar ++ sequence prepends --";
[A ++ 3, 3 ++ A, [1] ++ "ab", "ab" ++ [1], A ++ null, null ++ A];
[L ++ 3, 9];
[3 ++ L, 9];
[type(L ++ 3), type(A ++ 3), type((1 to 2) ++ 3)]
"-- scalar ++ scalar is text; the same text kind is kept --";
["a" ++ "b", 'a' ++ 'b', 1 ++ 2, "n=" ++ 1, 'a' ++ "b", type('a' ++ 'b'), type('a' ++ "b")];
[(b'\x01' ++ b'\x02') == b'\x0102', type(b'\x01' ++ b'\x02')]
"-- null is the identity --";
[null ++ 3, 3 ++ null, "a" ++ null, null ++ "a", (null ++ null) == null]
"-- numeric arrays of different element types (LR05-9) --";
[len([1, 2] ++ [3.5]), ([1, 2] ++ [3.5])[2], ([1, 2] ++ [3.5])[0] == 1, ([1.5] ++ [2])[1] == 2, ([1i8, 2i8] ++ [300])[2]]

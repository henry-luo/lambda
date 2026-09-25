// S10.1.5v3: `that` is the single-value proviso. `x that p` is x when p --
// with ~ bound to x -- is truthy, and null otherwise. The left operand is one
// item whatever it is: a collection is not walked (filtering its members is
// `|:`, S10.1.6). A failed proviso is absence, never an error.

"-- scalar --";
5 that ~ > 3;
[5 that ~ > 9];
["x" that len(~) == 1]

"-- a collection is one item --";
[1, 2, 3] that len(~) > 2;
[[1, 2, 3] that len(~) > 5];
{name: "a", age: 30} that ~.age > 18;
[(1, 2, 3) that len(~) == 3];
[null that true]

"-- composes with or and if --";
(7 that ~ > 9) or 0;
[if (7 that ~ > 3) "kept" else "dropped", if (7 that ~ > 9) "kept" else "dropped"]

"-- pipe tier, left-associative --";
[1, 2, 3] |> ~ * 2 that len(~) == 3;
[1, 2, 3, 4] |: ~ > 1 that len(~) == 3;
[4, 5] |> ~ + 1 that ~[0] == 5 |> ~ * 10

"-- ~ is the operand; ~key is null (S10.1.3) --";
5 that ~key == null;
[1, 5, 10] |: (~ that ~ > 3) != null;
[[1], [1, 2]] |> (~ that len(~) > 1)

"-- typed operands --";
let n: int = 7
let kept = n that ~ > 3;
(kept or 0) + 1
let gone = n that ~ > 9;
[gone]
let t: int[] = [4, 5, 6];
(t that len(~) == 3)[1]

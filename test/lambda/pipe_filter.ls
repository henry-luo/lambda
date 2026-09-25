// S10.1.6: `|:` is the filter stage of the pipe family. `c |: p` walks what
// `c |> body` walks, binds `~` to each member and `~key` to its key or index,
// and keeps the members for which `p` is truthy. Its result kind is the
// mapping pipe's, by reference: an array for every sequence source, a list
// included, which never collapses (S10.1.2v4, S2.5.7v2).

"-- walk --";
[1, 2, 3, 4, 5] |: ~ > 2
1 to 10 |: ~ % 3 == 0
{a: 1, b: 2, c: 3} |: ~ != 2;
(<div "a" <br> "c">) |: ~ is string;
["banana" |: ~ != "a"]

"-- key and index --";
[10, 20, 30] |: ~key > 0
{a: 1, b: 2, c: 3} |: ~key != 'b'

"-- empty and scalar sources --";
[1, 2, 3] |: ~ > 10;
[] |: ~ > 0;
["" |: ~ != "x"]
7 |: ~ > 3;
[7 |: ~ > 9];
[null |: ~ > 0]

"-- a list source gives an array (S2.5.7v2) --";
(1, 2, 3) |: ~ > 1;
(1, 2, 3) |: ~ > 2;
(1, 2, 3) |: ~ > 5;
(1, 2, 3) |> ~ * 2

"-- chains left to right at the pipe tier --";
[1, 2, 3, 4, 5, 6] |: ~ % 2 == 0 |> ~ * 10 |: ~ > 20;
[1, 2, 3, 4] |: ~ > 1 and ~ < 4
let rows = [{n: "a", v: 3}, {n: "b", v: 8}, {n: "c", v: 5}]
rows |: ~.v > 4 |> ~.n

"-- a line may begin with |: (S16.2.2v3) --";
let picked = rows
    |: ~.v > 4
    |> ~.n
picked

"-- nested: the innermost ~ wins (S10.1.3) --";
[[1, 2], [3, 4, 5], [6]] |: len(~ |: ~ > 2) > 1;
[[1, 2], [3, 4]] |> (~ |: ~ % 2 == 1)

"-- typed sources --";
let a: int[] = [1, 2, 3, 4, 5, 6]
a |: ~ % 2 == 0
let f: float[] = [0.5, 1.5, 2.5]
f |: ~ > 1.0
fn is_even(n: int) bool => n % 2 == 0;
a |: is_even(~)

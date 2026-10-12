'=== of membership ==='
// S8.1.1v2: `of` is position membership, the IntKey class. The left operand
// normalizes as a subscript does (S8.2.1v4); `in` tests values, `at` names.
let xs = [10, 20, 30]
0 of xs
2 of xs
3 of xs;
-1 of xs
1.0 of xs
1.5 of xs
"1" of xs
20 of xs
20 in xs
0 of (5 to 7)
2 of (5 to 7)
3 of (5 to 7)
2 of "abc"
3 of "abc"
0 of []

let m = {a: 1, b: 2}
0 of m
'a' of m
'a' at m

let el = <item id: "i1", "child" <b "x">>
0 of el
1 of el
2 of el
'id' of el
'id' at el
1 at el

0 of 5
0 of null
// a VMap's IntKeys are keys, not positions, so they may be negative (S8.1.2v3);
// `at` admits its names only, by contents, whether spelled as symbol or string
let vm = map([1, "one", "a", 2, -3, "neg"])
1 of vm
1.0 of vm;
-3 of vm
2 of vm
1 at vm
'a' at vm
"a" at vm
'a' of vm

'=== of iteration ===';
// S8.1.3v2: one name binds the position, two names bind (position, value)
[for (i of xs) i];
[for (i, x of xs) i * x];
[for (i of "abc") i];
[for (i of 5 to 7) i];
[for (i of m) i];
[for (i, c of el) i];
[for (k at el) k];
[for (k, v in el) k];
[for (i of xs where i > 0) xs[i]];
// an index annotation that restates the axis is fine; on `in` it narrows
[for (i: int, x of xs) i];
[for (i: int, x in el) i];
[for (k: symbol, v in el) k]
// `of` is a bindable word, like `in` and `at` (S16.10.1v3)
// a line-leading `of` continues the previous line (S16.2.2), hence the `;`
let of = 7;
of + 1;
[for (of of [1, 2]) of]
'=== membership mirrors iteration ===';
[for (i of el) i of el];
[for (i, x of xs) (i of xs) and (x in xs)]

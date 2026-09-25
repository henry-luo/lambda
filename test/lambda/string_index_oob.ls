// S7.2.1: an out-of-range subscript is absence, for text as for every other
// sequence — both failure directions are symmetric, so `s[-1]` and `s[len]`
// are null. `""` stays the empty *result* of a text operation (S7.10.1v2),
// which is what a filter that keeps nothing yields.

let ascii = "abc"
let unicode = "é";

[
    ascii[3] == null,
    ascii[-1] == null,
    unicode[1] == null,
    type(ascii[3]),
    ("abc" |: ~ == "z") == ""
]

// len(x) must equal the number of iterations `for (i in x)` performs.
// Each row prints len first, then the items the loop actually yields, so the
// golden records the correspondence rather than just the count.
//
// This law caught a real bug: len(map) was hardcoded to 0 while `for` walked
// the map's entries, so a populated map compared equal to an empty one.

"-- text: iterates chars --"
len("str"); [for (i in "str") i]

"-- array: shallow --"
len([1, 2, 3]); [for (i in [1, 2, 3]) i]
len([[1, 2], [3]]); [for (i in [[1, 2], [3]]) i]

"-- range --"
len(1 to 3); [for (i in 1 to 3) i]

"-- map: entry count, values on iteration --"
len({}); [for (i in {}) i]
len({a: 1}); [for (i in {a: 1}) i]
len({a: 1, b: 2, c: 3}); [for (i in {a: 1, b: 2, c: 3}) i]
len({a: null, b: 2}); [for (i in {a: null, b: 2}) i]

"-- absent --"
len(null); [for (i in null) i]

"-- element: attribute values then content, len matches the walk --"
len(<elmt a: 1, b: 2, "text">); [for (i in <elmt a: 1, b: 2, "text">) i]

"-- count(): splicing is syntactic, not a value property (see 8.3) --"
// a for-expression and a spread splice at the construction site...
len([1, for (x in [2, 3]) x, 4])
let spliced = [2, 3]
len([1, *spliced, 4])
// ...but binding either yields an ordinary value that counts as one item
let bound_for = for (x in [2, 3]) x
len([1, bound_for, 4])
let bound_spread = *spliced
len([1, bound_spread, 4])

"-- 4.8: an int range needs a successor, so its bounds are band-limited --"
// consecutive integers exist only to 2^53; these are all in band
len(1 to 5)
len(9007199254740989 to 9007199254740991)     // the 2^53-1 edge
// beyond it the request stops denoting a sequence and is refused; a larger
// sequence is written `1n to N` over `integer`, exact at every magnitude.

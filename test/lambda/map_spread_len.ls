// len() and for-iteration over spread-built maps must see the FLATTENED
// distinct-key field sequence (S8.3.1: len(x) is the number of iterations
// `for (i in x)` performs), not the literal's raw shape entries — a spread is
// one nameless Map*-link slot covering many fields. Map keys are unique, so a
// name written both by a spread and directly is one field, read last-writer-wins.

let base = {x: 10, y: 20, z: 30}
len(base)

// spread + new key: 4 fields, not 2 shape entries
let b = {*:base, w: 5}
len(b)

// iteration walks the spread's fields too, in flattened order
let bvals = for (v in b) v;
[bvals]

// key shadowed through a spread counts once; the literal's write wins
let shadow = {*:base, x: 99}
len(shadow)
let svals = for (v in shadow) v;
[svals]

// spread after an explicit key: still 2 distinct fields, spread value wins
let after = {p: 100, *:{p: 1, q: 2}}
len(after)
let avals = for (v in after) v;
[avals]

// nested spread-of-spread flattens recursively
let deep = {*:b, q: 1}
len(deep)

// spreading an empty map adds nothing
let none = {*:{}, only: 1}
len(none)

// element attribute spread uses the same link slots; `at` iteration must
// list the spread's attribute names
let src = <node id: "a", custom: 42, "old">
let rebuilt = <node *:map(src), "new">
let names = for (k at rebuilt) k;
[names]
rebuilt.id
rebuilt.custom

// Map field lookup must keep last-writer-wins over the whole shape chain,
// including duplicate literal keys and fields shadowed through a spread.
// Pins the semantics the Tune6 L1 keyed lookup path has to preserve.

// duplicate literal keys — the later entry wins
let d1 = {a: 1, a: 2, b: 3}
d1.a
d1.b

// duplicate keys with a type change between them
let d2 = {x: "str", x: 30}
d2.x
let d3 = {y: 7, y: "seven"}
[d3.y]

// three writers of the same key
let d4 = {k: 1, k: 2, k: 3}
d4.k

// explicit key after a spread overrides the spread's field
let base = {p: 1, q: 2}
let s1 = {*:base, p: 100}
s1.p
s1.q

// spread after an explicit key — the spread wins
let s2 = {p: 100, *:base}
s2.p

// duplicate key on both sides of a spread
let s3 = {p: 10, *:base, p: 999}
s3.p

// nested spread chain — the outermost writer still wins
let mid = {*:base, q: 20}
let s4 = {*:mid, q: 200}
s4.p
s4.q

// a key present only in the spread is still reachable
let s5 = {*:base, r: 3}
s5.p
s5.q
s5.r

// missing key on a map with duplicates stays absent
let miss = {a: 1, a: 2}
[miss.zzz]

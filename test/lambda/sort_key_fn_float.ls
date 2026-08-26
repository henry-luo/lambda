// Keyed sort must honor a FLOAT key function [Type_Infer Impl §18].
//
// Before the emit_double_bits carrier fix, `sort(xs, fn)` silently ignored a
// key function returning a float: the key was read as boxed-Item bits rather
// than as a double, so the comparison fell back to the values' natural order.
// The graphviz layout goldens had baked that in. Each case below is chosen so
// that honoring the key gives a DIFFERENT answer from natural order.

// strictly decreasing float key while ids ascend -> honoring the key reverses
let a = [for (e in sort([{s: 3.0, id: "a"}, {s: 2.0, id: "b"}, {s: 1.0, id: "c"}],
    (e) => e.s)) e.id]

// float key disagreeing with natural (field-order) comparison
let b = [for (e in sort([{s: 1.0, id: "z"}, {s: 2.0, id: "a"}], (e) => e.s)) e.id]

// an int key function must keep working
let c = [for (e in sort([{n: 3, id: "a"}, {n: 1, id: "b"}], (e) => e.n)) e.id]

// ties keep input order (stable)
let d = [for (e in sort([{s: 0.0, id: "x"}, {s: 0.0, id: "a"}, {s: 1.0, id: "m"}],
    (e) => e.s)) e.id];

[a, b, c, d]

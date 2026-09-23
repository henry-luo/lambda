// P0 fixture of vibe/impl/Lambda_List_Fixes.md — lives in test/lambda/ext until its
// phase turns it green, then moves to test/lambda (baseline). Golden written from the
// rulings, not from the runtime.
// S2.5.6: a list is transient — where it lands decides. A single-value slot
// of a persistent container stores the array image; bindings, arguments and
// returns pass the list through; it prints and formats as its array; a
// top-level list is content (S2.6.1). (phase P3; print/format half P1)
// Green on both tiers after P3 (2026-09-23); moved from test/lambda/ext.

let l = (1, 2)
"-- single-value slots store the array image; the source stays a list --"
let m = {f: l}
let e = <e a: l>;
[type(m.f), m.f, type(e.a), e.a, type(l)];
[m.f, 9];
[e.a, 9];
[l, 9]
let m2 = {g: for (x in [7, 8]) x, h: for (x in []) x, i: for (x in [5]) x};
[type(m2.g), m2.g, m2.h == null, m2.i]
"-- bindings, arguments, returns pass a list through --"
fn id(v) => v
fn mk() => (4, 5);
[type(id(l)), type(mk())];
[id(l), 9];
[mk(), 9]
"-- print/format as the array --";
[string(l), format(l, 'json') == format([1, 2], 'json')]
"-- a top-level list is content: one item per line --"
l;
(l, 3)

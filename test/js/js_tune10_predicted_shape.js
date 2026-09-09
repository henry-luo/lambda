// T10-2: object literals allocated on a compile-predicted shape must be
// observationally identical to ones built by incremental adds.

// order and membership
var a = { x: 1, y: 2, z: 3 };
console.log(Object.keys(a).join(","), JSON.stringify(a));

// the same site evaluated with different value types must not leak a slot type
function make(v) { return { p: v, q: 7 }; }
var m1 = make(1), m2 = make("s"), m3 = make(null), m4 = make({ n: 1 });
console.log(JSON.stringify(m1), JSON.stringify(m2), JSON.stringify(m3), JSON.stringify(m4));
console.log(typeof m1.p, typeof m2.p, m3.p, JSON.stringify(m4.p));

// an unwritten slot must be absent, never null: `q` exists, `r` never did
console.log("q" in m1, "r" in m1, m1.r, Object.keys(m1).join(","));

// delete then re-add keeps insertion order semantics
var d = { k1: 1, k2: 2 };
delete d.k1;
console.log(Object.keys(d).join(","), "k1" in d);
d.k1 = 9;
console.log(Object.keys(d).join(","), d.k1);

// shorthand and computed/spread/accessor/proto forms all stay correct
var v1 = 10, v2 = 20;
var sh = { v1, v2 };
console.log(JSON.stringify(sh));
var key = "c";
var comp = { a: 1, [key]: 2 };
console.log(Object.keys(comp).join(","), JSON.stringify(comp));
var spread = { ...sh, w: 3 };
console.log(Object.keys(spread).join(","), JSON.stringify(spread));
var acc = { get g() { return 42; }, h: 1 };
console.log(acc.g, acc.h, Object.keys(acc).join(","));
var proto = { __proto__: { inherited: 5 }, own: 1 };
console.log(proto.inherited, proto.own, Object.keys(proto).join(","));

// duplicate keys collapse to one property, last value wins
var dup = { e: 1, e: 2 };
console.log(Object.keys(dup).join(","), dup.e);

// descriptors on a shaped instance stay default
var desc = Object.getOwnPropertyDescriptor(a, "x");
console.log(desc.writable, desc.enumerable, desc.configurable, desc.value);

// freeze and redefine still detach correctly
var f = { s: 1 };
Object.freeze(f);
f.s = 99;
console.log(f.s, Object.isFrozen(f));

// nested literals and for-in order
var nested = { outer: { inner: 1 }, tail: 2 };
var seen = [];
for (var k in nested) seen.push(k);
console.log(seen.join(","), nested.outer.inner, nested.tail);

// two instances share one predicted shape: no path may cross-retag it in place,
// or the sibling's slot is reinterpreted through the new tag
function mk(v) { return { a: v, b: 1 }; }
var o1 = mk(11), o2 = mk(22);
Object.assign(o2, { a: "str" });
console.log("assign", o1.a, typeof o1.a, o2.a, typeof o2.a);
var o3 = mk(33), o4 = mk(44);
o4.a = "direct";
console.log("direct", o3.a, typeof o3.a, o4.a, typeof o4.a);
var o5 = mk(55), o6 = mk(66);
Object.defineProperty(o6, "a", { value: {n:1}, writable: true, enumerable: true, configurable: true });
console.log("defprop", o5.a, typeof o5.a, JSON.stringify(o6.a));

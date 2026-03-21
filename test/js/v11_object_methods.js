// v11: Object.fromEntries and Object.is

// Object.fromEntries
let entries = [["a", 1], ["b", 2], ["c", 3]];
let obj = Object.fromEntries(entries);
console.log("fromEntries a:", obj.a);
console.log("fromEntries b:", obj.b);
console.log("fromEntries c:", obj.c);

// Round-trip: Object.entries → Object.fromEntries
let orig = { x: 10, y: 20 };
let roundtrip = Object.fromEntries(Object.entries(orig));
console.log("roundtrip x:", roundtrip.x);
console.log("roundtrip y:", roundtrip.y);

// Object.is — same as === except for NaN and +0/-0
console.log("is(1, 1):", Object.is(1, 1));
console.log("is(1, 2):", Object.is(1, 2));
console.log("is(NaN, NaN):", Object.is(NaN, NaN));
console.log("is(null, null):", Object.is(null, null));
console.log("is(null, undefined):", Object.is(null, undefined));
console.log("is('a', 'a'):", Object.is("a", "a"));

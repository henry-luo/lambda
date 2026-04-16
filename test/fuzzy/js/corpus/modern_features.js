// Edge case: optional chaining, nullish coalescing, Map/Set
var obj = {a: {b: {c: 42}}};
obj?.a?.b?.c;
obj?.x?.y?.z;
null?.x;
undefined?.x;
var arr = [1, 2, 3];
arr?.[0];
arr?.[999];
null?.[0];

var fn = function() { return 1; };
fn?.();
null?.();
undefined?.();

// Nullish coalescing
null ?? "default";
undefined ?? "default";
0 ?? "default";
"" ?? "default";
false ?? "default";

// Logical assignment
var a = null;
a ??= 42;
a;
var b = 0;
b ||= 10;
b;
var c = 1;
c &&= 2;
c;

// Map
var m = new Map();
m.set("key", "value");
m.set(42, "num");
m.set(null, "null");
m.get("key");
m.get(42);
m.has("key");
m.has("missing");
m.size;
m.delete("key");
m.size;

// Set
var s = new Set([1, 2, 3, 2, 1]);
s.size;
s.has(1);
s.has(4);
s.add(4);
s.size;
s.delete(1);
s.size;

// WeakMap / WeakRef (basic)
var wm = new Map();  // WeakMap if supported
var k = {};
wm.set(k, "data");
wm.get(k);

// for..of on Map/Set
for (var [key, val] of m) {}
for (var item of s) {}

// Map from entries
var m2 = new Map([["a", 1], ["b", 2]]);
m2.get("a");

// Spread Map/Set
var arr2 = [...s];
var arr3 = [...m2];

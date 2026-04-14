// WeakMap, WeakSet, Map, and Set advanced tests

// --- Test 1: WeakMap basic ---
var wm = new WeakMap();
var k1 = {};
var k2 = {};
wm.set(k1, "hello");
wm.set(k2, 42);
console.log("t1:" + wm.get(k1) + "," + wm.get(k2) + "," + wm.has(k1));

// --- Test 2: WeakMap delete ---
wm.delete(k1);
console.log("t2:" + wm.has(k1) + "," + wm.has(k2));

// --- Test 3: WeakSet basic ---
var ws = new WeakSet();
var o1 = {};
var o2 = {};
ws.add(o1);
ws.add(o2);
console.log("t3:" + ws.has(o1) + "," + ws.has(o2) + "," + ws.has({}));

// --- Test 4: WeakSet delete ---
ws.delete(o1);
console.log("t4:" + ws.has(o1) + "," + ws.has(o2));

// --- Test 5: Map ordering ---
var m = new Map();
m.set("c", 3);
m.set("a", 1);
m.set("b", 2);
var keys = [];
m.forEach(function(v, k) { keys.push(k); });
console.log("t5:" + keys.join(","));

// --- Test 6: Map with various key types ---
var m2 = new Map();
m2.set(1, "number");
m2.set("1", "string");
m2.set(true, "bool");
console.log("t6:" + m2.get(1) + "," + m2.get("1") + "," + m2.get(true) + "," + m2.size);

// --- Test 7: Set uniqueness ---
var s = new Set([1, 2, 3, 2, 1, 3, 4]);
console.log("t7:" + s.size);
var vals = [];
s.forEach(function(v) { vals.push(v); });
console.log("t7b:" + vals.join(","));

// --- Test 8: Set operations ---
var s2 = new Set();
s2.add("x");
s2.add("y");
s2.add("z");
s2.delete("y");
console.log("t8:" + s2.has("x") + "," + s2.has("y") + "," + s2.has("z") + "," + s2.size);

// --- Test 9: Map from entries ---
var m3 = new Map([["x", 10], ["y", 20], ["z", 30]]);
console.log("t9:" + m3.get("x") + "," + m3.get("y") + "," + m3.get("z") + "," + m3.size);

// --- Test 10: Set from iterable ---
var s3 = new Set("hello");
var chars = [];
s3.forEach(function(c) { chars.push(c); });
console.log("t10:" + chars.join(",") + "," + s3.size);

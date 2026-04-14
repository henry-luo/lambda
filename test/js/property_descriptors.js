// Property descriptors and object meta-programming

// --- Test 1: Object.defineProperty ---
var obj = {};
Object.defineProperty(obj, "x", { value: 42, writable: false, enumerable: true, configurable: false });
console.log("t1:" + obj.x);

// --- Test 2: non-writable property ---
var o2 = {};
Object.defineProperty(o2, "name", { value: "fixed", writable: false });
try { "use strict"; o2.name = "changed"; } catch (e) { /* may or may not throw */ }
console.log("t2:" + o2.name);

// --- Test 3: Object.getOwnPropertyDescriptor ---
var o3 = { a: 10 };
var desc = Object.getOwnPropertyDescriptor(o3, "a");
console.log("t3:" + desc.value + "," + desc.writable + "," + desc.enumerable + "," + desc.configurable);

// --- Test 4: non-enumerable property ---
var o4 = {};
Object.defineProperty(o4, "hidden", { value: "secret", enumerable: false });
o4.visible = "public";
console.log("t4:" + Object.keys(o4).join(","));
console.log("t4b:" + o4.hidden);

// --- Test 5: accessor descriptor ---
var o5 = {};
var _backing = 0;
Object.defineProperty(o5, "val", {
  get: function() { return _backing; },
  set: function(v) { _backing = v * 2; },
  enumerable: true,
  configurable: true
});
o5.val = 5;
console.log("t5:" + o5.val);

// --- Test 6: Object.freeze ---
var frozen = Object.freeze({ a: 1, b: 2 });
console.log("t6:" + frozen.a + "," + Object.isFrozen(frozen));

// --- Test 7: Object.keys / values / entries ---
var o7 = { x: 1, y: 2, z: 3 };
console.log("t7k:" + Object.keys(o7).join(","));
console.log("t7v:" + Object.values(o7).join(","));
console.log("t7e:" + Object.entries(o7).map(function(e) { return e[0] + "=" + e[1]; }).join(","));

// --- Test 8: Object.assign ---
var target = { a: 1 };
Object.assign(target, { b: 2 }, { c: 3, a: 99 });
console.log("t8:" + target.a + "," + target.b + "," + target.c);

// --- Test 9: Object.create with prototype ---
var proto = { greet: function() { return "hello " + this.name; } };
var o9 = Object.create(proto);
o9.name = "world";
console.log("t9:" + o9.greet());
console.log("t9b:" + (o9.hasOwnProperty("name")) + "," + (o9.hasOwnProperty("greet")));

// --- Test 10: delete operator ---
var o10 = { a: 1, b: 2, c: 3 };
var deleted = delete o10.b;
console.log("t10:" + deleted + "," + ("b" in o10) + "," + Object.keys(o10).join(","));

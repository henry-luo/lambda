// JSON advanced tests - parse/stringify with options, edge cases

// --- Test 1: JSON.stringify basic types ---
console.log("t1a:" + JSON.stringify(42));
console.log("t1b:" + JSON.stringify("hello"));
console.log("t1c:" + JSON.stringify(true));
console.log("t1d:" + JSON.stringify(null));

// --- Test 2: JSON.stringify object ---
console.log("t2:" + JSON.stringify({a: 1, b: "hi", c: true, d: null}));

// --- Test 3: JSON.stringify array ---
console.log("t3:" + JSON.stringify([1, "two", true, null]));

// --- Test 4: JSON.stringify nested ---
console.log("t4:" + JSON.stringify({a: {b: {c: 42}}}));

// --- Test 5: JSON.stringify with replacer function ---
var obj = {a: 1, b: 2, c: 3, d: 4};
var json5 = JSON.stringify(obj, function(key, value) {
  if (typeof(value) === "number" && value > 2) return undefined;
  return value;
});
console.log("t5:" + json5);

// --- Test 6: JSON.parse basic ---
var p6 = JSON.parse('{"name":"John","age":30}');
console.log("t6:" + p6.name + "," + p6.age);

// --- Test 7: JSON.parse with reviver ---
var p7 = JSON.parse('{"a":1,"b":2,"c":3}', function(key, value) {
  return typeof(value) === "number" ? value * 10 : value;
});
console.log("t7:" + p7.a + "," + p7.b + "," + p7.c);

// --- Test 8: JSON roundtrip ---
var original = {
  nums: [1, 2, 3],
  str: "hello",
  flag: true,
  nil: null,
  nested: { x: 10, y: 20 }
};
var roundtrip = JSON.parse(JSON.stringify(original));
console.log("t8a:" + roundtrip.nums.join(","));
console.log("t8b:" + roundtrip.str + "," + roundtrip.flag + "," + roundtrip.nil);
console.log("t8c:" + roundtrip.nested.x + "," + roundtrip.nested.y);

// --- Test 9: JSON.parse array ---
var p9 = JSON.parse('[1, 2, "three", true, null]');
console.log("t9:" + p9.length + "," + p9[0] + "," + p9[2] + "," + p9[3] + "," + p9[4]);

// --- Test 10: JSON.stringify undefined and function omission ---
var obj10 = {a: 1, b: undefined, c: function() {}, d: "ok"};
console.log("t10:" + JSON.stringify(obj10));

// Spread, rest, and Array.from/Array.isArray tests

// --- Test 1: array spread ---
var a = [1, 2];
var b = [3, 4];
var c = [...a, ...b, 5];
console.log("t1:" + c.join(","));

// --- Test 2: object spread ---
var o1 = {a: 1, b: 2};
var o2 = {...o1, c: 3, b: 99};
console.log("t2:" + o2.a + "," + o2.b + "," + o2.c);

// --- Test 3: spread in function call ---
function sum(a, b, c) { return a + b + c; }
var args = [10, 20, 30];
console.log("t3:" + sum(...args));

// --- Test 4: Array.from string ---
console.log("t4:" + Array.from("hello").join(","));

// --- Test 5: Array.from with map ---
console.log("t5:" + Array.from([1, 2, 3], function(x) { return x * x; }).join(","));

// --- Test 6: Array.isArray ---
console.log("t6:" + Array.isArray([]) + "," + Array.isArray({}) + "," + Array.isArray("hello") + "," + Array.isArray(null));

// --- Test 7: spread to clone array ---
var orig = [1, 2, 3];
var clone = [...orig];
clone.push(4);
console.log("t7:" + orig.join(",") + " | " + clone.join(","));

// --- Test 8: spread to clone object ---
var src = {x: 1, y: 2};
var copy = {...src};
copy.z = 3;
console.log("t8:" + JSON.stringify(src) + " | " + JSON.stringify(copy));

// --- Test 9: rest in destructuring ---
var {a: first, ...rest} = {a: 1, b: 2, c: 3};
console.log("t9:" + first + "," + JSON.stringify(rest));

// --- Test 10: nested spread ---
var matrix = [[1, 2], [3, 4]];
var flat = [].concat(...matrix);
console.log("t10:" + flat.join(","));

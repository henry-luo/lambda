// Operator edge cases: void, typeof, delete, comma, ternary, short-circuit

// --- Test 1: void operator ---
console.log("t1:" + (void 0 === undefined));
console.log("t1b:" + (void "hello" === undefined));
console.log("t1c:" + (void 42 === undefined));

// --- Test 2: typeof operator ---
console.log("t2a:" + typeof(undefined));
console.log("t2b:" + typeof(42));
console.log("t2c:" + typeof("hello"));
console.log("t2d:" + typeof(true));
console.log("t2e:" + typeof(null));
console.log("t2f:" + typeof({}));
console.log("t2g:" + typeof([]));
console.log("t2h:" + typeof(function() {}));
console.log("t2i:" + typeof(Symbol("x")));

// --- Test 3: delete operator ---
var obj = {a: 1, b: 2, c: 3};
console.log("t3a:" + delete obj.b);
console.log("t3b:" + ("b" in obj));
console.log("t3c:" + Object.keys(obj).join(","));

// --- Test 4: comma operator ---
var x = (1, 2, 3);
console.log("t4:" + x);
var y = 0;
for (var i = 0, j = 10; i < 3; i++, j--) { y = y + i + j; }
console.log("t4b:" + y);

// --- Test 5: ternary chains ---
function grade(score) {
  return score >= 90 ? "A" : score >= 80 ? "B" : score >= 70 ? "C" : score >= 60 ? "D" : "F";
}
console.log("t5:" + grade(95) + "," + grade(85) + "," + grade(75) + "," + grade(65) + "," + grade(55));

// --- Test 6: short-circuit AND ---
var log = [];
function track(val, name) { log.push(name); return val; }
track(false, "a") && track(true, "b");
console.log("t6:" + log.join(","));

// --- Test 7: short-circuit OR ---
log = [];
track(true, "c") || track(true, "d");
console.log("t7:" + log.join(","));

// --- Test 8: nullish coalescing ---
console.log("t8a:" + (null ?? "default"));
console.log("t8b:" + (undefined ?? "default"));
console.log("t8c:" + (0 ?? "default"));
console.log("t8d:" + ("" ?? "default"));
console.log("t8e:" + (false ?? "default"));

// --- Test 9: optional chaining ---
var deep = {a: {b: {c: 42}}};
console.log("t9a:" + deep?.a?.b?.c);
console.log("t9b:" + (deep?.x?.y?.z === undefined));

// --- Test 10: in operator ---
var obj2 = {x: 1, y: 2};
console.log("t10:" + ("x" in obj2) + "," + ("z" in obj2));
var arr = [10, 20, 30];
console.log("t10b:" + (0 in arr) + "," + (5 in arr));

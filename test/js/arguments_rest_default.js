// arguments object and rest/default parameters

// --- Test 1: arguments object basic ---
function sum() {
  var total = 0;
  for (var i = 0; i < arguments.length; i++) {
    total += arguments[i];
  }
  return total;
}
console.log("t1:" + sum(1, 2, 3, 4, 5));

// --- Test 2: arguments.length ---
function argCount() { return arguments.length; }
console.log("t2:" + argCount() + "," + argCount(1) + "," + argCount(1, 2, 3));

// --- Test 3: rest parameters ---
function first(a, ...rest) {
  return a + ":[" + rest.join(",") + "]";
}
console.log("t3:" + first(1, 2, 3, 4));

// --- Test 4: rest is a real array ---
function isArr(...args) {
  return Array.isArray(args) + "," + args.length;
}
console.log("t4:" + isArr(1, 2, 3));

// --- Test 5: default parameters ---
function greet(name, greeting = "Hello") {
  return greeting + " " + name;
}
console.log("t5a:" + greet("World"));
console.log("t5b:" + greet("World", "Hi"));
console.log("t5c:" + greet("World", undefined));

// --- Test 6: default with expression ---
function makeArray(val, count = 3) {
  var arr = [];
  for (var i = 0; i < count; i++) arr.push(val);
  return arr.join(",");
}
console.log("t6:" + makeArray("x") + " | " + makeArray("y", 2));

// --- Test 7: rest parameter with spread ---
function mirror(...args) { return args; }
var m = mirror(1, 2, 3);
var spread = [...m, ...m];
console.log("t7:" + spread.join(","));

// --- Test 8: arguments with named params ---
function mix(a, b) {
  return a + "," + b + "," + arguments.length;
}
console.log("t8:" + mix(1, 2, 3, 4));

// --- Test 9: rest in arrow function ---
var sum2 = (...nums) => {
  var t = 0;
  for (var n of nums) t += n;
  return t;
};
console.log("t9:" + sum2(10, 20, 30));

// --- Test 10: default parameter with function call ---
function defaultCount() { return 5; }
function repeat(str, times = defaultCount()) {
  return str.repeat(times);
}
console.log("t10:" + repeat("ab") + "," + repeat("x", 3));

// Destructuring advanced: defaults, nesting, patterns, swap

// --- Test 1: object destructuring with defaults ---
var {a = 10, b = 20, c = 30} = {a: 1, c: 3};
console.log("t1:" + a + "," + b + "," + c);

// --- Test 2: array destructuring with skip ---
var [x, , z] = [1, 2, 3];
console.log("t2:" + x + "," + z);

// --- Test 3: nested object destructuring ---
var {p: {q, r = 99}} = {p: {q: 42}};
console.log("t3:" + q + "," + r);

// --- Test 4: array rest destructuring ---
var [head, ...tail] = [1, 2, 3, 4, 5];
console.log("t4:" + head + ",[" + tail.join(",") + "]");

// --- Test 5: swap via destructuring ---
var m = 1, n = 2;
[m, n] = [n, m];
console.log("t5:" + m + "," + n);

// --- Test 6: function parameter destructuring ---
function point({x, y}) {
  return "(" + x + "," + y + ")";
}
console.log("t6:" + point({x: 3, y: 4}));

// --- Test 7: array parameter destructuring ---
function first([a, b]) {
  return a + "+" + b;
}
console.log("t7:" + first([10, 20]));

// --- Test 8: destructuring in for-of ---
var pairs = [[1, "a"], [2, "b"], [3, "c"]];
var result = [];
for (var [num, letter] of pairs) {
  result.push(letter + num);
}
console.log("t8:" + result.join(","));

// --- Test 9: computed property destructuring ---
var key = "name";
var {[key]: val} = {name: "Alice"};
console.log("t9:" + val);

// --- Test 10: complex nested ---
var data = {
  users: [{name: "A", age: 30}, {name: "B", age: 25}],
  count: 2
};
var {users: [{name: firstName}, {age: secondAge}], count} = data;
console.log("t10:" + firstName + "," + secondAge + "," + count);

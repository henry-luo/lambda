// v18 Destructuring Assignment Tests
// Covers patterns fixed in the destructuring assignment bug fix

// === Array destructuring ASSIGNMENT (not declaration) ===
var a, b;
[a, b] = [1, 2];
console.log("arr assign:", a, b);

// With default values
var p, q;
[p = 100, q = 200] = [42];
console.log("arr defaults:", p, q);

// Default not applied when value is null (only undefined triggers default)
var m, n;
[m = 10, n = 20] = [null, 0];
console.log("arr no-default:", m, n);

// Elision (skipping elements)
var x, y;
[, x, , y] = [1, 2, 3, 4];
console.log("arr elision:", x, y);

// === Object destructuring ASSIGNMENT ===
var ox, oy;
({ox, oy} = {ox: 10, oy: 20});
console.log("obj assign:", ox, oy);

// Object with rename
var ra, rb;
({a: ra, b: rb} = {a: 100, b: 200});
console.log("obj rename:", ra, rb);

// === Declaration-style destructuring (always worked, regression test) ===
const [da, db, dc] = [7, 8, 9];
console.log("decl array:", da, db, dc);

const {ka, kb} = {ka: "hello", kb: "world"};
console.log("decl obj:", ka, kb);

// === For-of with array destructuring + defaults ===
var results = [];
for (var [v1, v2 = 99] of [[1, 2], [3]]) {
    results.push(v1 + ":" + v2);
}
console.log("for-of defaults:", results.join(" "));

// For-of with object destructuring + defaults
var names = [];
for (var {name, score = 0} of [{name: "Alice", score: 95}, {name: "Bob"}]) {
    names.push(name + "=" + score);
}
console.log("for-of obj:", names.join(" "));

// For-of with rest element (declaration style)
var heads = [];
var tails = [];
for (var [h, ...t] of [[1, 2, 3], [4, 5, 6, 7]]) {
    heads.push(h);
    tails.push(t.length);
}
console.log("for-of rest:", heads.join(","), tails.join(","));

// === Swap via destructuring ===
var s1 = "hello", s2 = "world";
[s1, s2] = [s2, s1];
console.log("swap:", s1, s2);

// === Destructuring from function return ===
function getCoord() { return [3, 7]; }
var cx, cy;
[cx, cy] = getCoord();
console.log("func return:", cx, cy);

// === Multiple assignments in sequence ===
var r1, r2, r3;
[r1] = [10];
[r2] = [20];
[r3] = [30];
console.log("sequential:", r1, r2, r3);

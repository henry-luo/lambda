// Edge case: destructuring, spread, rest
var [a, b, ...rest] = [1, 2, 3, 4, 5];
a; b; rest;

var {x, y: renamed, z = 99} = {x: 1, y: 2};
x; renamed; z;

// Nested destructuring
var {a: {b: {c: deep}}} = {a: {b: {c: 42}}};
deep;

// Swap
var p = 1, q = 2;
[p, q] = [q, p];
p; q;

// Spread in arrays
var arr = [1, ...[2, 3], 4];
arr;

// Spread in objects
var o1 = {a: 1, b: 2};
var o2 = {...o1, c: 3, b: 99}; // b overwritten
o2;

// Spread in function calls
function sum3(a, b, c) { return a + b + c; }
sum3(...[1, 2, 3]);

// Rest parameters
function collect(...args) { return args; }
collect(1, 2, 3);

// Destructuring in params
function greet({name, age = 0}) { return name + " " + age; }
greet({name: "Alice", age: 30});
greet({name: "Bob"});

// Destructure from null/undefined (should throw)
try { var {a} = null; } catch(e) {}
try { var [a] = undefined; } catch(e) {}

// Iterator destructuring
var [first, , third] = [1, 2, 3]; // skip second
first; third;

// Default with complex expression
var [x = (function() { return 42; })()] = [];
x;

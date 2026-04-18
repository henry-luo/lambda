// Test: Array constructor called indirectly via variable alias
// Section: array_ctor_indirect
var il = Array;
var r1 = il(3);
console.log(r1 !== null && r1 !== undefined && r1.length === 3);
var r2 = il(0);
console.log(r2 !== null && r2 !== undefined && r2.length === 0);
var r3 = Array(3);
console.log(r3 !== null && r3 !== undefined && r3.length === 3);

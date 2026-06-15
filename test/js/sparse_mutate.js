// Js58 P1: mutating Array methods must handle sparse companion-map DATA
// entries without dense-buffer OOB reads or writes.

// --- fill across dense range only ---
var a1 = [1, 2, 3];
a1[100] = "x";   // gap=97 → dense
a1.fill(99, 0, 5);
console.log("fill-a1[0]", a1[0]);
console.log("fill-a1[3]", a1[3]);
console.log("fill-a1[100]", a1[100]);

// --- reverse a tiny dense array ---
var a2 = [1, 2, 3];
a2.reverse();
console.log("reverse-a2[0]", a2[0]);
console.log("reverse-a2[2]", a2[2]);

// --- sort a tiny dense array ---
var a3 = [30, 10, 20];
a3.sort(function (x, y) { return x - y; });
console.log("sort-a3[0]", a3[0]);
console.log("sort-a3[2]", a3[2]);

// --- copyWithin ---
var a4 = [1, 2, 3, 4, 5];
a4.copyWithin(0, 3);
console.log("copy-a4[0]", a4[0]);
console.log("copy-a4[1]", a4[1]);
console.log("copy-a4[4]", a4[4]);

// --- fill across sparse-only range ---
var a5 = [1, 2, 3];
a5[1000004] = "x";
a5.fill("f", 1000003, 1000005);
console.log("fill-a5[1000003]", a5[1000003]);
console.log("fill-a5[1000004]", a5[1000004]);

// --- copyWithin from sparse source ---
var a6 = [1, 2, 3];
a6[1000004] = "x";
a6.copyWithin(0, 1000004);
console.log("copy-a6[0]", a6[0]);

// --- reverse sparse entry to front and dense front to sparse end ---
var a7 = [1, 2, 3];
a7[1000004] = "x";
a7.reverse();
console.log("reverse-a7[0]", a7[0]);
console.log("reverse-a7[1000004]", a7[1000004]);

// --- sort sparse entries with dense present values, holes at end ---
var a8 = [3, 1, 2];
a8[1000004] = 0;
a8.sort(function (x, y) { return x - y; });
console.log("sort-a8[0]", a8[0]);
console.log("sort-a8[3]", a8[3]);
console.log("sort-a8[4]", a8[4]);
console.log("sort-a8-length", a8.length);

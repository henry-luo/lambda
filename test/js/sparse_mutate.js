// Js58 P1 target: fill / copyWithin / reverse / sort on sparse arrays.
//
// CURRENT behaviour mostly correct for in-range dense slots; sparse
// entries beyond capacity are touched only by paths that consult the
// companion Map. The .txt below pins observed behaviour today; Phase 1's
// refactor should keep these passing while extending the sparse semantics.

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

// Js58 P2: slice / splice must carry sparse companion-map entries without
// reading past the dense items buffer.

var arr = [10, 20, 30];
arr[20000] = "sparse";

// slice the dense range — no sparse should appear
var s1 = arr.slice(0, 100);
console.log("slice-100-length", s1.length);
console.log("slice-100[0]", s1[0]);
console.log("slice-100[2]", s1[2]);
console.log("slice-100[50]", s1[50]);

// slice the full array — sparse value must be copied to the same relative index
var s2 = arr.slice(0, arr.length);
console.log("slice-all-length", s2.length);
console.log("slice-all[0]", s2[0]);
console.log("slice-all[20000]", s2[20000]);

// splice — remove 2 from start, no sparse entries in [0,2)
var arr2 = [1, 2, 3];
arr2[20000] = "x";
var removed = arr2.splice(0, 2);
console.log("splice-removed-length", removed.length);
console.log("splice-arr2-length", arr2.length);
console.log("splice-arr2[0]", arr2[0]);
console.log("splice-arr2[19998]", arr2[19998]);
console.log("splice-arr2[20000]", arr2[20000]);

// splice — remove a range containing a sparse entry
var arr3 = [1, 2, 3];
arr3[20000] = "y";
var removed2 = arr3.splice(19999, 2);
console.log("splice-removed2-length", removed2.length);
console.log("splice-removed2[0]", removed2[0]);
console.log("splice-removed2[1]", removed2[1]);
console.log("splice-arr3-length", arr3.length);
console.log("splice-arr3[20000]", arr3[20000]);

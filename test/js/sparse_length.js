// Js58 P3 target: arr.length = N on sparse arrays.
//
// CURRENT behaviour: setting arr.length = N to truncate doesn't iterate
// the companion Map to delete entries at index >= N. Direct sparse access
// (arr[1000004]) returns undefined because of Js58 P0's bounds-check guard,
// but Object.keys-style enumeration also happens to filter via length.
//
// Phase 3 wires js_array_delete_sparse_indices_from into the truncation
// branch so the Map state matches the spec — no orphans, no surprises if
// someone bumps length back up later.

var arr = [1, 2, 3];
arr[1000004] = "z";
console.log("before-length", arr.length);
console.log("before-arr[1000004]", arr[1000004]);

arr.length = 100;
console.log("after-length", arr.length);
console.log("after-arr[1000004]", arr[1000004]);
console.log("after-arr[2]", arr[2]);
console.log("after-keys-length", Object.keys(arr).length);

// length extension: holes, not sparse-revived
arr.length = 1100000;
console.log("ext-length", arr.length);
console.log("ext-arr[1000004]", arr[1000004]);   // should stay undefined (was already deleted)

// length = 0 — full clear
var arr2 = [1, 2, 3];
arr2[1000004] = "y";
arr2.length = 0;
console.log("zero-length", arr2.length);
console.log("zero-arr2[0]", arr2[0]);
console.log("zero-keys-length", Object.keys(arr2).length);

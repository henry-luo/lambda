// Baseline sparse-array invariants — these guard the runtime's read-path
// behaviour after Js58 P0's hole-fill on sparse-store and the OOB-bounds
// guards added to js_in / js_array_element / js_object_keys.

var arr = [10, 20, 30];
arr[20000] = "sparse";

console.log("length", arr.length);
console.log("arr[0]", arr[0]);
console.log("arr[2]", arr[2]);
console.log("arr[3]", arr[3]);           // hole between dense and sparse
console.log("arr[100]", arr[100]);       // hole
console.log("arr[19999]", arr[19999]);   // hole just before sparse
console.log("arr[20000]", arr[20000]);   // sparse value
console.log("arr[20001]", arr[20001]);   // past length

console.log("0-in-arr", 0 in arr);
console.log("3-in-arr", 3 in arr);
console.log("100-in-arr", 100 in arr);
console.log("20000-in-arr", 20000 in arr);
console.log("20001-in-arr", 20001 in arr);

console.log("typeof-arr[20000]", typeof arr[20000]);
console.log("typeof-arr[3]", typeof arr[3]);

// Re-assignment to existing sparse index
arr[20000] = "updated";
console.log("after-update", arr[20000]);

// New sparse index
arr[30000] = "another";
console.log("length-after-2nd", arr.length);
console.log("arr[30000]", arr[30000]);
console.log("arr[20000]", arr[20000]);

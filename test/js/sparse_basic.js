// Baseline sparse-array invariants — these guard the runtime's read-path
// behaviour after Js58 P0's hole-fill on sparse-store and the OOB-bounds
// guards added to js_in / js_array_element / js_object_keys.

var arr = [10, 20, 30];
arr[1000004] = "sparse";

console.log("length", arr.length);
console.log("arr[0]", arr[0]);
console.log("arr[2]", arr[2]);
console.log("arr[3]", arr[3]);           // hole between dense and sparse
console.log("arr[100]", arr[100]);       // hole
console.log("arr[1000003]", arr[1000003]);   // hole just before sparse
console.log("arr[1000004]", arr[1000004]);   // sparse value
console.log("arr[1000005]", arr[1000005]);   // past length

console.log("0-in-arr", 0 in arr);
console.log("3-in-arr", 3 in arr);
console.log("100-in-arr", 100 in arr);
console.log("1000004-in-arr", 1000004 in arr);
console.log("1000005-in-arr", 1000005 in arr);

console.log("typeof-arr[1000004]", typeof arr[1000004]);
console.log("typeof-arr[3]", typeof arr[3]);

// Re-assignment to existing sparse index
arr[1000004] = "updated";
console.log("after-update", arr[1000004]);

// New sparse index
arr[1100004] = "another";
console.log("length-after-2nd", arr.length);
console.log("arr[1100004]", arr[1100004]);
console.log("arr[1000004]", arr[1000004]);

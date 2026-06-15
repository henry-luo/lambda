// Js58 P2 target: Array.prototype.includes on sparse arrays.
//
// CURRENT behaviour: includes() routes through a fast path that gates on
// `arr->extra == 0`. When extra is non-zero (sparse mode), it falls
// through to js_array_element which doesn't consult the companion Map's
// data entries. Result: sparse values are not found.
//
// When Js58 P2 lands (one-line fix to js_array_method_has_property), this
// .txt file must be updated to:
//   includes-sparse true     ← currently "false"
//   includes-30 true
//   includes-missing false

var arr = [10, 20, 30];
arr[20000] = "sparse";

console.log("includes-sparse", arr.includes("sparse"));
console.log("includes-30", arr.includes(30));
console.log("includes-missing", arr.includes("missing"));
console.log("includes-undefined", arr.includes(undefined));

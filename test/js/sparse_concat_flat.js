// Js58 regression-lock: concat / flat / flatMap on sparse arrays.
//
// These DO work today via the property-get delegation in
// js_array_method_has_property → js_has_property. Pin them so Phase 1's
// helper refactor doesn't regress them.

var arr = [10, 20, 30];
arr[1000004] = "sparse";

// concat: [99] + arr -> length 1+1000005, sparse at index 1+1000004
var combined = [99].concat(arr);
console.log("concat-length", combined.length);
console.log("concat[0]", combined[0]);
console.log("concat[1]", combined[1]);
console.log("concat-sparse-index", combined.indexOf("sparse"));

// flat: a small sparse inner array — holes are spec-skipped by flat
var inner = [10, 20];
inner[100] = "x";   // gap=98 < SPARSE_GAP_MAX, stays dense
var flat = [[1], inner, [3]].flat();
console.log("flat-length", flat.length);
console.log("flat-x-index", flat.indexOf("x"));

// flatMap on a sparse array
var arr2 = [1, 2, 3];
arr2[1000004] = "z";
var out = arr2.flatMap(function (v) { return [v]; });
console.log("flatMap-length", out.length);
console.log("flatMap-find-z", out.indexOf("z"));

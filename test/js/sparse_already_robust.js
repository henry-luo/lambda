// Js58 regression-lock for the sparse-array iteration paths that were already
// routed through js_array_find_next_own_element after Js57 P8 + Js58 P0
// (hole-fill on sparse-store, OOB guards on js_array_element / js_in /
// js_object_keys). These paths must stay correct as other sparse methods are
// fixed in sibling fixtures.
//
// Methods covered here:  every, some, forEach, map, filter, indexOf,
//                        lastIndexOf, Object.keys, `in` operator, delete.
//
// Methods covered by sibling fixtures: find, findIndex, findLast,
// findLastIndex, reduce, reduceRight, includes, flat, flatMap, concat,
// slice, splice, fill, copyWithin, reverse, sort, length-truncate.

var arr = [10, 20, 30];
arr[20000] = "sparse";   // gap=19997 > SPARSE_GAP_MAX=10000 → routes through extra Map

console.log("length", arr.length);

var everyCalls = 0;
var everyResult = arr.every(function () { everyCalls++; return true; });
console.log("every", everyResult, "calls", everyCalls);

var someCalls = 0;
var someResult = arr.some(function (v) { someCalls++; return v === "sparse"; });
console.log("some", someResult, "calls", someCalls);

var seen = [];
arr.forEach(function (v, i) { seen.push(i + "=" + v); });
console.log("forEach", seen.join("|"));

var mapped = arr.map(function (v) { return typeof v; });
console.log("map.length", mapped.length, "[0]", mapped[0], "[20000]", mapped[20000]);

var filtered = arr.filter(function () { return true; });
console.log("filter.length", filtered.length);

console.log("indexOf-sparse", arr.indexOf("sparse"));
console.log("indexOf-30", arr.indexOf(30));
console.log("indexOf-missing", arr.indexOf("missing"));
console.log("lastIndexOf-sparse", arr.lastIndexOf("sparse"));

var keys = Object.keys(arr);
console.log("keys-includes-20000", keys.indexOf("20000") >= 0);
console.log("keys-includes-0", keys.indexOf("0") >= 0);

console.log("0-in", 0 in arr);
console.log("20000-in", 20000 in arr);
console.log("9999-in", 9999 in arr);

console.log("delete-sparse", delete arr[20000]);
console.log("after-delete-in", 20000 in arr);
console.log("after-delete-value", arr[20000]);

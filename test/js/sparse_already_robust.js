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
arr[1000004] = "sparse";   // gap=1000001 > SPARSE_GAP_MAX, routes through extra Map

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
console.log("map.length", mapped.length, "[0]", mapped[0], "[1000004]", mapped[1000004]);

var filtered = arr.filter(function () { return true; });
console.log("filter.length", filtered.length);

console.log("indexOf-sparse", arr.indexOf("sparse"));
console.log("indexOf-30", arr.indexOf(30));
console.log("indexOf-missing", arr.indexOf("missing"));
console.log("lastIndexOf-sparse", arr.lastIndexOf("sparse"));

var keys = Object.keys(arr);
console.log("keys-includes-1000004", keys.indexOf("1000004") >= 0);
console.log("keys-includes-0", keys.indexOf("0") >= 0);

console.log("0-in", 0 in arr);
console.log("1000004-in", 1000004 in arr);
console.log("9999-in", 9999 in arr);

console.log("delete-sparse", delete arr[1000004]);
console.log("after-delete-in", 1000004 in arr);
console.log("after-delete-value", arr[1000004]);

// Phase 4 sparse-key cursor safety: callbacks can still add/delete future
// sparse entries while iteration is in progress.
var dynAdd = [];
dynAdd[1000004] = "a";
dynAdd[1100004] = "z";
var dynAddSeen = [];
dynAdd.forEach(function (v, i) {
  dynAddSeen.push(i + "=" + v);
  if (i === 1000004) dynAdd[1050004] = "new";
});
console.log("dynamic-add", dynAddSeen.join("|"));

var dynDel = [];
dynDel[1000004] = "a";
dynDel[1050004] = "drop";
dynDel[1100004] = "z";
var dynDelSeen = [];
dynDel.forEach(function (v, i) {
  dynDelSeen.push(i + "=" + v);
  if (i === 1000004) delete dynDel[1050004];
});
console.log("dynamic-delete", dynDelSeen.join("|"));

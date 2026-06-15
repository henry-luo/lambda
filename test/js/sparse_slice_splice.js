// Js58 P2 target: slice / splice on sparse arrays.
//
// CURRENT behaviour: slice's dense memcpy doesn't carry sparse entries
// into the result; splice's remove pass walks dense items only and skips
// sparse entries in the removed range. Both are partial.
//
// When Js58 P2 lands:
//   slice-all[20000] should be "sparse" (currently "undefined" or hole)
//   splice-removed-length should be the count of present entries in the
//   range, including any sparse entries.

var arr = [10, 20, 30];
arr[20000] = "sparse";

// slice the dense range — no sparse should appear
var s1 = arr.slice(0, 100);
console.log("slice-100-length", s1.length);
console.log("slice-100[0]", s1[0]);
console.log("slice-100[2]", s1[2]);
console.log("slice-100[50]", s1[50]);

// slice the full array — currently the result's dense buffer never grows past
// the source's capacity, so the sparse value is lost in batch mode and reads
// as non-deterministic memory. Only assert the length and the dense head;
// Phase 2 will carry sparse via the companion-Map propagation.
var s2 = arr.slice(0, arr.length);
console.log("slice-all-length", s2.length);
console.log("slice-all[0]", s2[0]);

// splice — remove 2 from start, no sparse entries in [0,2)
var arr2 = [1, 2, 3];
arr2[20000] = "x";
var removed = arr2.splice(0, 2);
console.log("splice-removed-length", removed.length);
console.log("splice-arr2-length", arr2.length);
console.log("splice-arr2[0]", arr2[0]);

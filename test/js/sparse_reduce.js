// Js58 P1 target: reduce / reduceRight on sparse arrays.
//
// reduce/reduceRight in Lambda actually route through js_property_get
// (via the js_array_method_has_property → js_has_property delegation),
// which DOES consult the companion-Map. Result: this method family is
// already correct on sparse arrays. This test pins that behaviour so
// Phase 1's `find_next_own_element` migration doesn't accidentally
// regress it.

var arr = [10, 20, 30];
arr[20000] = 200000;

// initial-value reduce: 0 + 10 + 20 + 30 + 200000 = 200060
console.log("reduce-sum", arr.reduce(function (a, v) { return a + v; }, 0));

// no-initial-value: accumulator starts at arr[0]=10, then folds the rest
console.log("reduce-no-init", arr.reduce(function (a, v) { return a + v; }));

// reduceRight visits sparse first, then dense right-to-left
console.log("reduceRight-keys",
    arr.reduceRight(function (acc, _v, i) { return acc + "-" + i; }, "start"));

// reduceRight with string-typed accumulator: should pick up "sparse"
var arr2 = [10, 20, 30];
arr2[20000] = "sparse";
console.log("reduceRight-first-string",
    arr2.reduceRight(function (a, v) { return a !== null ? a : (typeof v === "string" ? v : null); }, null));

// Js58 P1 target: find / findIndex on sparse arrays.
//
// CURRENT (Js57 P8 + Js58 P0) behaviour. Both functions use
// js_array_element internally; that helper reads accessors out of the
// companion Map but skips DATA entries, so a sparse value at index 20000
// is unreachable. find/findIndex return undefined/-1 for predicates that
// can only match the sparse entry.
//
// When Js58 P1 lands, this .txt file must be updated to:
//   find-30 30
//   find-string sparse        ← currently "undefined"
//   findIndex-string 20000    ← currently "-1"

var arr = [10, 20, 30];
arr[20000] = "sparse";

console.log("find-30", arr.find(function (v) { return v === 30; }));
console.log("find-string", arr.find(function (v) { return typeof v === "string"; }));
console.log("findIndex-string", arr.findIndex(function (v) { return typeof v === "string"; }));
console.log("find-missing", arr.find(function (v) { return v === 999; }));

// Js58 P1: find / findIndex / findLast / findLastIndex must read sparse
// companion-map DATA entries, not just dense items.

var arr = [10, 20, 30];
arr[20000] = "sparse";

console.log("find-30", arr.find(function (v) { return v === 30; }));
console.log("find-string", arr.find(function (v) { return typeof v === "string"; }));
console.log("findIndex-string", arr.findIndex(function (v) { return typeof v === "string"; }));
console.log("findLast-string", arr.findLast(function (v) { return typeof v === "string"; }));
console.log("findLastIndex-string", arr.findLastIndex(function (v) { return typeof v === "string"; }));
console.log("find-missing", arr.find(function (v) { return v === 999; }));

// Js58 P2: Array.prototype.includes must see sparse companion-map DATA entries.

var arr = [10, 20, 30];
arr[20000] = "sparse";

console.log("includes-sparse", arr.includes("sparse"));
console.log("includes-30", arr.includes(30));
console.log("includes-missing", arr.includes("missing"));
console.log("includes-undefined", arr.includes(undefined));

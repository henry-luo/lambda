// v9 Number static methods, Array.from

// Number.isInteger
console.log("isInteger 5:", Number.isInteger(5));
console.log("isInteger 5.5:", Number.isInteger(5.5));

// Number.isFinite
console.log("isFinite 42:", Number.isFinite(42));

// Number.isNaN
console.log("isNaN 42:", Number.isNaN(42));

// Number.isSafeInteger
console.log("isSafe 42:", Number.isSafeInteger(42));

// Lambda int64 host values egress to JS BigInt, never Number
let hostInt64 = JSON.parse("9007199254740993");
console.log("host int64 typeof:", typeof hostInt64);
console.log("host int64 ctor:", hostInt64.constructor === BigInt);
console.log("host int64 isInteger:", Number.isInteger(hostInt64));
try {
  JSON.stringify(hostInt64);
  console.log("host int64 json: no throw");
} catch (e) {
  console.log("host int64 json:", e.name);
}
console.log("host int64 strict:", hostInt64 === 9007199254740993n);
console.log("bigint radix 64:", (18446744073709551615n).toString(16));
console.log("bigint asUintN64:", BigInt.asUintN(64, -1n).toString(16));

// Array.from array
let src = [10, 20, 30];
let copy = Array.from(src);
console.log("from array:", copy.join(","));
console.log("from len:", copy.length);

// Array.from string
let chars = Array.from("hi");
console.log("from string:", chars.join(","));

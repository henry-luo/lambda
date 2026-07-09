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
const bufferUint64 = Buffer.alloc(8);
bufferUint64.writeBigUInt64BE(18446744073709551615n, 0);
console.log("buffer biguint64:", typeof bufferUint64.readBigUInt64BE(0), bufferUint64.readBigUInt64BE(0).toString(16));
const bufferInt64 = Buffer.alloc(8);
bufferInt64.writeBigInt64BE(-1n, 0);
console.log("buffer bigint64:", typeof bufferInt64.readBigInt64BE(0), bufferInt64.readBigInt64BE(0).toString());
try {
  Buffer.alloc(8).writeBigInt64BE("1", 0);
  console.log("buffer bigint64 string: no throw");
} catch (e) {
  console.log("buffer bigint64 string:", e.name);
}
const dataView64 = new DataView(new ArrayBuffer(8));
dataView64.setBigUint64(0, 18446744073709551615n);
console.log("dataview biguint64:", typeof dataView64.getBigUint64(0), dataView64.getBigUint64(0).toString(16));
dataView64.setBigInt64(0, -1n);
console.log("dataview bigint64:", typeof dataView64.getBigInt64(0), dataView64.getBigInt64(0).toString());
const typedBigUint64 = new BigUint64Array(1);
typedBigUint64[0] = 18446744073709551615n;
console.log("typed biguint64:", typeof typedBigUint64[0], typedBigUint64[0].toString(16));
const typedBigInt64 = new BigInt64Array(1);
typedBigInt64[0] = -1n;
console.log("typed bigint64:", typeof typedBigInt64[0], typedBigInt64[0].toString());

// Array.from array
let src = [10, 20, 30];
let copy = Array.from(src);
console.log("from array:", copy.join(","));
console.log("from len:", copy.length);

// Array.from string
let chars = Array.from("hi");
console.log("from string:", chars.join(","));

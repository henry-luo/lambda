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
console.log("bigint shift neg typeof:", typeof (-(1n << 63n)));
console.log("bigint shift neg:", (-(1n << 63n)).toString());
console.log("bigint asIntN64 min:", BigInt.asIntN(64, -(1n << 63n)).toString());
const hugeUintHex = BigInt.asUintN(8000, -1n).toString(16);
console.log("bigint asUintN8000 hex:", hugeUintHex === "f".repeat(2000));
const hugeDecimalSource = "1" + "0".repeat(4096);
const hugeDecimalBigInt = BigInt(hugeDecimalSource);
console.log("bigint parse decimal4097:", hugeDecimalBigInt.toString().length, hugeDecimalBigInt.toString() === hugeDecimalSource);
const hugeHexBigInt = BigInt("0x" + "f".repeat(4096));
console.log("bigint parse hex4096:", hugeHexBigInt.toString(16).length, hugeHexBigInt.toString(16) === "f".repeat(4096));
const hugeShiftHex = (1n << 8000n).toString(16);
console.log("bigint shift8000 hex:", hugeShiftHex.length, hugeShiftHex[0], hugeShiftHex.slice(1) === "0".repeat(2000));
console.log("bigint pow huge identity:", (1n ** (1n << 63n)).toString());
try {
  BigInt.asIntN(1n, 0n);
  console.log("bigint asIntN bits bigint: no throw");
} catch (e) {
  console.log("bigint asIntN bits bigint:", e.name);
}
try {
  1n + 1;
  console.log("bigint mix add: no throw");
} catch (e) {
  console.log("bigint mix add:", e.name);
}
try {
  1n >>> 0n;
  console.log("bigint unsigned shift: no throw");
} catch (e) {
  console.log("bigint unsigned shift:", e.name);
}
const bufferUint64 = Buffer.alloc(8);
bufferUint64.writeBigUInt64BE(18446744073709551615n, 0);
console.log("buffer biguint64:", typeof bufferUint64.readBigUInt64BE(0), bufferUint64.readBigUInt64BE(0).toString(16));
const bufferInt64 = Buffer.alloc(8);
bufferInt64.writeBigInt64BE(-1n, 0);
console.log("buffer bigint64:", typeof bufferInt64.readBigInt64BE(0), bufferInt64.readBigInt64BE(0).toString());
bufferInt64.writeBigInt64BE(-(1n << 63n), 0);
console.log("buffer bigint64 min:", typeof bufferInt64.readBigInt64BE(0), bufferInt64.readBigInt64BE(0).toString());
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
dataView64.setBigInt64(0, -(1n << 63n));
console.log("dataview bigint64 min:", typeof dataView64.getBigInt64(0), dataView64.getBigInt64(0).toString());
const typedBigUint64 = new BigUint64Array(1);
typedBigUint64[0] = 18446744073709551615n;
console.log("typed biguint64:", typeof typedBigUint64[0], typedBigUint64[0].toString(16));
const typedBigInt64 = new BigInt64Array(1);
typedBigInt64[0] = -1n;
console.log("typed bigint64:", typeof typedBigInt64[0], typedBigInt64[0].toString());
typedBigInt64[0] = -(1n << 63n);
console.log("typed bigint64 min:", typeof typedBigInt64[0], typedBigInt64[0].toString());
const atomicsBigInt64 = new BigInt64Array(new SharedArrayBuffer(8));
atomicsBigInt64[0] = 1n;
console.log("atomics bigint add:", Atomics.add(atomicsBigInt64, 0, 2n).toString(), atomicsBigInt64[0].toString());
try {
  Atomics.add(atomicsBigInt64, 0, 1);
  console.log("atomics bigint number: no throw");
} catch (e) {
  console.log("atomics bigint number:", e.name);
}

// Array.from array
let src = [10, 20, 30];
let copy = Array.from(src);
console.log("from array:", copy.join(","));
console.log("from len:", copy.length);

// Array.from string
let chars = Array.from("hi");
console.log("from string:", chars.join(","));

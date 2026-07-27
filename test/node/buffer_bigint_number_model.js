// Buffer BigInt accessors are Node compatibility, not JavaScript runtime coverage.
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

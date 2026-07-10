function assertEq(actual, expected, label) {
    if (actual !== expected) {
        throw new Error(label + ": got " + String(actual) + " expected " + String(expected));
    }
}

const overU64 = (1n << 64n) + 5n;
const overI64 = (1n << 63n) + 7n;

{
    const values = new BigUint64Array(2);
    values.fill(overU64);
    assertEq(values[0], 5n, "BigUint64Array.fill wraps first element");
    assertEq(values[1], 5n, "BigUint64Array.fill wraps second element");
}

{
    const values = new BigInt64Array(2);
    values.fill(overI64);
    assertEq(values[0], -9223372036854775801n, "BigInt64Array.fill wraps first element");
    assertEq(values[1], -9223372036854775801n, "BigInt64Array.fill wraps second element");
}

{
    const values = new BigUint64Array(1);
    values[0] = overU64;
    assertEq(values[0], 5n, "BigUint64Array index assignment still wraps");
}

console.log("number model BigInt typed-array wrap: ok");

// A const BigInt binding is an immutable GC-owned decimal Item. Returning it
// must not reserve a scalar return home solely because the expression is a name.
function assertEq(actual, expected, label) {
    if (actual !== expected) {
        throw new Error(label + ": got " + String(actual) + " expected " + String(expected));
    }
}

function stableBigIntReturn() {
    const value = 9223372036854775807n;
    return value;
}

assertEq(stableBigIntReturn(), 9223372036854775807n,
    "const BigInt return retains its exact value");
console.log("phase7 stable BigInt return: ok");

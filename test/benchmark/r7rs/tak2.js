// R7RS Benchmark: tak (Node.js)
// Takeuchi function - tak(18, 12, 6) = 7
'use strict';

function tak(x, y, z) {
    if (y >= x) return z;
    const a = tak(x - 1, y, z);
    const b = tak(y - 1, z, x);
    const c = tak(z - 1, x, y);
    return tak(a, b, c);
}

function main() {
    const __t0 = process.hrtime.bigint();
    const result = tak(18, 12, 6);
    const __t1 = process.hrtime.bigint();

    if (result === 7) {
        process.stdout.write("tak: PASS\n");
    } else {
        process.stdout.write("tak: FAIL result=" + result + "\n");
    }
    process.stdout.write("__TIMING__:" + Number(__t1 - __t0) / 1e6 + "\n");
}

main();

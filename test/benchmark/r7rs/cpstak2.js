// R7RS Benchmark: cpstak (Node.js)
// Double-run Takeuchi function - tak(18, 12, 6) = 7, run twice
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
    let result = tak(18, 12, 6);
    result = tak(18, 12, 6);
    const __t1 = process.hrtime.bigint();

    if (result === 7) {
        process.stdout.write("cpstak: PASS\n");
    } else {
        process.stdout.write("cpstak: FAIL result=" + result + "\n");
    }
    process.stdout.write("__TIMING__:" + Number(__t1 - __t0) / 1e6 + "\n");
}

main();

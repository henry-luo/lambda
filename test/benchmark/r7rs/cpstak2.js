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
    const __t0 = performance.now();
    let result = tak(18, 12, 6);
    result = tak(18, 12, 6);
    const __t1 = performance.now();

    if (result === 7) {
        process.stdout.write("cpstak: PASS\n");
    } else {
        process.stdout.write("cpstak: FAIL result=" + result + "\n");
    }
    process.stdout.write("__TIMING__:" + (__t1 - __t0) + "\n");
}

main();

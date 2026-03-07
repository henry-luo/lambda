// R7RS Benchmark: fibfp (Node.js)
// Fibonacci using floating-point arithmetic - fibfp(27.0) = 196418.0
'use strict';

function fibfp(n) {
    if (n < 2.0) return n;
    return fibfp(n - 1.0) + fibfp(n - 2.0);
}

function main() {
    const __t0 = process.hrtime.bigint();
    const result = fibfp(27.0);
    const __t1 = process.hrtime.bigint();

    if (result === 196418.0) {
        process.stdout.write("fibfp: PASS\n");
    } else {
        process.stdout.write("fibfp: FAIL result=" + result + "\n");
    }
    process.stdout.write("__TIMING__:" + Number(__t1 - __t0) / 1e6 + "\n");
}

main();

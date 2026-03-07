// R7RS Benchmark: sumfp (Node.js)
// Sum of floats from 0.0 to 100000.0
'use strict';

function run(n) {
    let i = n;
    let s = 0.0;
    while (i >= 0.0) {
        s = s + i;
        i = i - 1.0;
    }
    return s;
}

function main() {
    const __t0 = process.hrtime.bigint();
    const result = run(100000.0);
    const __t1 = process.hrtime.bigint();

    const expected = 5000050000.0;
    if (Math.abs(result - expected) < 1.0) {
        process.stdout.write("sumfp: PASS\n");
    } else {
        process.stdout.write("sumfp: FAIL result=" + result + "\n");
    }
    process.stdout.write("__TIMING__:" + Number(__t1 - __t0) / 1e6 + "\n");
}

main();

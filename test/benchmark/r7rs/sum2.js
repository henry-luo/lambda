// R7RS Benchmark: sum (Node.js)
// Sum of integers from 0 to 10000, repeated 100 times
'use strict';

function run(n) {
    let i = n;
    let s = 0;
    while (i >= 0) {
        s = s + i;
        i = i - 1;
    }
    return s;
}

function main() {
    const __t0 = process.hrtime.bigint();
    let result = 0;
    for (let iter = 0; iter < 100; iter++) {
        result = run(10000);
    }
    const __t1 = process.hrtime.bigint();

    if (result === 50005000) {
        process.stdout.write("sum: PASS\n");
    } else {
        process.stdout.write("sum: FAIL result=" + result + "\n");
    }
    process.stdout.write("__TIMING__:" + Number(__t1 - __t0) / 1e6 + "\n");
}

main();

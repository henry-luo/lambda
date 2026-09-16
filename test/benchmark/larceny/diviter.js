// Larceny Benchmark: diviter (Node.js)
// Iterative integer division via repeated subtraction
'use strict';

function diviterDiv(x, y) {
    let q = 0;
    let r = x;
    while (r >= y) {
        r -= y;
        q++;
    }
    return q;
}

function diviterMod(x, y) {
    let r = x;
    while (r >= y) {
        r -= y;
    }
    return r;
}

function main() {
    const __t0 = performance.now();
    let result = 0;
    for (let iter = 0; iter < 1000; iter++) {
        result += diviterDiv(1000000, 2);
        result -= diviterMod(1000000, 2);
    }
    const __t1 = performance.now();
    if (result === 500000000) {
        process.stdout.write("diviter: PASS\n");
    } else {
        process.stdout.write("diviter: FAIL result=" + result + "\n");
    }
    process.stdout.write("__TIMING__:" + (__t1 - __t0) + "\n");
}

main();

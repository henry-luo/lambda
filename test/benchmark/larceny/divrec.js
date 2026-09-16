// Larceny Benchmark: divrec (Node.js)
// Recursive integer division via repeated subtraction
'use strict';

function divrecDivHelper(x, y, q) {
    if (x < y) return q;
    return divrecDivHelper(x - y, y, q + 1);
}

function divrecDiv(x, y) {
    return divrecDivHelper(x, y, 0);
}

function divrecMod(x, y) {
    if (x < y) return x;
    return divrecMod(x - y, y);
}

function main() {
    const __t0 = performance.now();
    let result = 0;
    for (let iter = 0; iter < 1000; iter++) {
        result += divrecDiv(1000, 2);
        result -= divrecMod(1000, 2);
    }
    const __t1 = performance.now();
    if (result === 500000) {
        process.stdout.write("divrec: PASS\n");
    } else {
        process.stdout.write("divrec: FAIL result=" + result + "\n");
    }
    process.stdout.write("__TIMING__:" + (__t1 - __t0) + "\n");
}

main();

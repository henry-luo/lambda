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
    let result = 0;
    for (let iter = 0; iter < 1000; iter++) {
        result += diviterDiv(1000000, 2);
        result -= diviterMod(1000000, 2);
    }
    if (result === 500000000) {
        process.stdout.write("diviter: PASS\n");
    } else {
        process.stdout.write("diviter: FAIL result=" + result + "\n");
    }
}

main();

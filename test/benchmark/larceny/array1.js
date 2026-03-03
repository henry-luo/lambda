// Larceny Benchmark: array1 (Node.js)
// Array creation, fill, and summation
'use strict';

function main() {
    const size = 10000;
    const arr = new Int32Array(size);

    for (let i = 0; i < size; i++) {
        arr[i] = i;
    }

    let total = 0;
    for (let iter = 0; iter < 100; iter++) {
        let s = 0;
        for (let i = 0; i < size; i++) {
            s += arr[i];
        }
        total = s;
    }
    if (total === 49995000) {
        process.stdout.write("array1: PASS\n");
    } else {
        process.stdout.write("array1: FAIL result=" + total + "\n");
    }
}

main();

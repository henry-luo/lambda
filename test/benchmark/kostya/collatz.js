// Kostya Benchmark: collatz (Node.js)
// Longest Collatz (3n+1) sequence under 1,000,000
'use strict';

function collatzLen(n) {
    let steps = 1;
    let x = n;
    while (x !== 1) {
        if (x % 2 === 0) {
            x = x / 2;  // avoid >> which truncates to 32-bit
        } else {
            x = 3 * x + 1;
        }
        steps++;
    }
    return steps;
}

function benchmark() {
    const limit = 1000000;
    let maxLen = 0;
    let maxStart = 0;
    for (let i = 1; i < limit; i++) {
        const clen = collatzLen(i);
        if (clen > maxLen) {
            maxLen = clen;
            maxStart = i;
        }
    }
    return maxStart;
}

function main() {
    const result = benchmark();
    if (result === 837799) {
        process.stdout.write("collatz: PASS (start=" + result + ")\n");
    } else {
        process.stdout.write("collatz: FAIL result=" + result + "\n");
    }
}

main();

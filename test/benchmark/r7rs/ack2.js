// R7RS Benchmark: ack (Node.js)
// Ackermann function - ack(3, 8) = 2045
'use strict';

function ack(m, n) {
    if (m === 0) return n + 1;
    if (n === 0) return ack(m - 1, 1);
    return ack(m - 1, ack(m, n - 1));
}

function main() {
    const __t0 = performance.now();
    const result = ack(3, 8);
    const __t1 = performance.now();

    if (result === 2045) {
        process.stdout.write("ack: PASS\n");
    } else {
        process.stdout.write("ack: FAIL result=" + result + "\n");
    }
    process.stdout.write("__TIMING__:" + (__t1 - __t0) + "\n");
}

main();

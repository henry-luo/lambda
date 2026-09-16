// R7RS Benchmark: fib (Node.js)
// Naive recursive Fibonacci - fib(27) = 196418
'use strict';

function fib(n) {
    if (n < 2) return n;
    return fib(n - 1) + fib(n - 2);
}

function main() {
    const __t0 = performance.now();
    const result = fib(27);
    const __t1 = performance.now();

    if (result === 196418) {
        process.stdout.write("fib: PASS\n");
    } else {
        process.stdout.write("fib: FAIL result=" + result + "\n");
    }
    process.stdout.write("__TIMING__:" + (__t1 - __t0) + "\n");
}

main();

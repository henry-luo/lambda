// Kostya Benchmark: primes (Node.js)
// Sieve of Eratosthenes counting primes up to 1,000,000
'use strict';

function sieve(limit) {
    const flags = new Uint8Array(limit + 1);
    flags.fill(1);
    flags[0] = 0;
    flags[1] = 0;
    for (let i = 2; i * i <= limit; i++) {
        if (flags[i]) {
            for (let j = i * i; j <= limit; j += i) {
                flags[j] = 0;
            }
        }
    }
    let count = 0;
    for (let i = 2; i <= limit; i++) {
        if (flags[i]) count++;
    }
    return count;
}

function main() {
    const __t0 = process.hrtime.bigint();
    const result = sieve(1000000);
    const __t1 = process.hrtime.bigint();
    if (result === 78498) {
        process.stdout.write("primes: PASS (" + result + ")\n");
    } else {
        process.stdout.write("primes: FAIL result=" + result + "\n");
    }
    process.stdout.write("__TIMING__:" + Number(__t1 - __t0) / 1e6 + "\n");
}

main();

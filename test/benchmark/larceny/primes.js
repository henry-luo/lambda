// Larceny Benchmark: primes (Node.js)
// Sieve of Eratosthenes to 8000, repeated 10 times
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
    let result = 0;
    for (let iter = 0; iter < 10; iter++) {
        result = sieve(8000);
    }
    if (result === 1007) {
        process.stdout.write("primes: PASS\n");
    } else {
        process.stdout.write("primes: FAIL result=" + result + "\n");
    }
}

main();

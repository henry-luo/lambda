// Kostya Benchmark: matmul (Node.js)
// Matrix multiplication — multiply two NxN matrices
'use strict';

const N = 200;

function nextRand(seed) {
    return (seed * 1664525 + 1013904223) % 1000000;
}

function matmul(a, b, c, n) {
    for (let i = 0; i < n; i++) {
        for (let j = 0; j < n; j++) {
            let sum = 0.0;
            for (let k = 0; k < n; k++) {
                sum += a[i * n + k] * b[k * n + j];
            }
            c[i * n + j] = sum;
        }
    }
}

function main() {
    const size = N * N;
    const a = new Float64Array(size);
    const b = new Float64Array(size);
    const c = new Float64Array(size);

    let seed = 42;
    for (let i = 0; i < size; i++) {
        seed = nextRand(seed);
        a[i] = (seed % 2000) / 1000.0 - 1.0;
        seed = nextRand(seed);
        b[i] = (seed % 2000) / 1000.0 - 1.0;
    }

    matmul(a, b, c, N);

    let total = 0.0;
    for (let i = 0; i < size; i++) {
        total += c[i];
    }

    const intTotal = Math.trunc(Math.floor(total));
    process.stdout.write("matmul: sum=" + intTotal + "\n");
    process.stdout.write("matmul: DONE\n");
}

main();

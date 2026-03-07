// R7RS Benchmark: mbrot (Node.js)
// Mandelbrot set generation - 1 iteration on 75x75 grid
// Expected: count at (0,0) = 5
'use strict';

function count(r, i, step, x, y) {
    const maxCount = 64;
    const radius2 = 16.0;
    const cr = r + x * step;
    const ci = i + y * step;
    let zr = cr;
    let zi = ci;
    let c = 0;
    while (c < maxCount) {
        const zr2 = zr * zr;
        const zi2 = zi * zi;
        if (zr2 + zi2 > radius2) return c;
        const newZr = zr2 - zi2 + cr;
        zi = 2.0 * zr * zi + ci;
        zr = newZr;
        c++;
    }
    return maxCount;
}

function mbrot(matrix, r, i, step, n) {
    for (let y = n - 1; y >= 0; y--) {
        for (let x = n - 1; x >= 0; x--) {
            matrix[x][y] = count(r, i, step, x, y);
        }
    }
}

function test(n) {
    const matrix = new Array(n);
    for (let idx = 0; idx < n; idx++) {
        matrix[idx] = new Int32Array(n);
    }
    mbrot(matrix, -1.0, -0.5, 0.005, n);
    return matrix[0][0];
}

function main() {
    const __t0 = process.hrtime.bigint();
    const result = test(75);
    const __t1 = process.hrtime.bigint();

    if (result === 5) {
        process.stdout.write("mbrot: PASS\n");
    } else {
        process.stdout.write("mbrot: FAIL result=" + result + "\n");
    }
    process.stdout.write("__TIMING__:" + Number(__t1 - __t0) / 1e6 + "\n");
}

main();

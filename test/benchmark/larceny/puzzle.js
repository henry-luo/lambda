// Larceny Benchmark: puzzle (Node.js)
// N-Queens n=10 — count all solutions
'use strict';

const BOARD_SIZE = 10;

function solve(row, cols, diag1, diag2, n) {
    if (row === n) return 1;
    let count = 0;
    for (let col = 0; col < n; col++) {
        const d1 = row + col;
        const d2 = row - col + n - 1;
        if (!cols[col] && !diag1[d1] && !diag2[d2]) {
            cols[col] = true;
            diag1[d1] = true;
            diag2[d2] = true;
            count += solve(row + 1, cols, diag1, diag2, n);
            cols[col] = false;
            diag1[d1] = false;
            diag2[d2] = false;
        }
    }
    return count;
}

function main() {
    const __t0 = process.hrtime.bigint();
    const cols = new Array(BOARD_SIZE).fill(false);
    const diag1 = new Array(BOARD_SIZE * 2).fill(false);
    const diag2 = new Array(BOARD_SIZE * 2).fill(false);

    const result = solve(0, cols, diag1, diag2, BOARD_SIZE);
    const __t1 = process.hrtime.bigint();
    if (result === 724) {
        process.stdout.write("puzzle: PASS\n");
    } else {
        process.stdout.write("puzzle: FAIL result=" + result + "\n");
    }
    process.stdout.write("__TIMING__:" + Number(__t1 - __t0) / 1e6 + "\n");
}

main();

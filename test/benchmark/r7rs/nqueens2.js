// R7RS Benchmark: nqueens (Node.js)
// Count all solutions to N-Queens problem - nqueens(8) = 92
'use strict';

function ok(row, dist, placed, placedLen) {
    if (dist > placedLen) return 1;
    const idx = placedLen - dist;
    const p = placed[idx];
    if (p === row + dist) return 0;
    if (p === row - dist) return 0;
    return ok(row, dist + 1, placed, placedLen);
}

function solve(candidates, candLen, rest, restLen, placed, placedLen) {
    if (candLen === 0) {
        if (restLen === 0) return 1;
        return 0;
    }
    const row = candidates[0];
    let count = 0;

    if (ok(row, 1, placed, placedLen) === 1) {
        const newCands = new Int32Array(candLen - 1 + restLen);
        let ni = 0;
        for (let ci = 1; ci < candLen; ci++) {
            newCands[ni++] = candidates[ci];
        }
        for (let ri = 0; ri < restLen; ri++) {
            newCands[ni++] = rest[ri];
        }
        placed[placedLen] = row;
        count += solve(newCands, ni, new Int32Array(1), 0, placed, placedLen + 1);
    }

    const newRest = new Int32Array(restLen + 1);
    for (let ri = 0; ri < restLen; ri++) {
        newRest[ri] = rest[ri];
    }
    newRest[restLen] = row;

    const newCands2 = new Int32Array(candLen - 1);
    let ni2 = 0;
    for (let ci = 1; ci < candLen; ci++) {
        newCands2[ni2++] = candidates[ci];
    }
    count += solve(newCands2, candLen - 1, newRest, restLen + 1, placed, placedLen);

    return count;
}

function nqueens(n) {
    const candidates = new Int32Array(n);
    for (let i = 0; i < n; i++) {
        candidates[i] = i + 1;
    }
    const placed = new Int32Array(n);
    const empty = new Int32Array(1);
    return solve(candidates, n, empty, 0, placed, 0);
}

function main() {
    const __t0 = process.hrtime.bigint();
    const result = nqueens(8);
    const __t1 = process.hrtime.bigint();

    if (result === 92) {
        process.stdout.write("nqueens: PASS\n");
    } else {
        process.stdout.write("nqueens: FAIL result=" + result + "\n");
    }
    process.stdout.write("__TIMING__:" + Number(__t1 - __t0) / 1e6 + "\n");
}

main();

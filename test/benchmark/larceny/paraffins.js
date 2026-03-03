// Larceny Benchmark: paraffins (Node.js)
// Count paraffin isomers using radical enumeration with multiset coefficients
'use strict';

function ms2(r) { return (r * (r + 1)) >> 1; }
function ms3(r) { return Math.trunc(r * (r + 1) * (r + 2) / 6); }
function ms4(r) { return Math.trunc(r * (r + 1) * (r + 2) * (r + 3) / 24); }

function countRadicals(rcount, k) {
    let count = 0;
    const target = k - 1;
    for (let nc1 = 0; nc1 * 3 <= target; nc1++) {
        for (let nc2 = nc1; nc1 + nc2 * 2 <= target; nc2++) {
            const nc3 = target - nc1 - nc2;
            if (nc3 >= nc2) {
                const r1 = rcount[nc1], r2 = rcount[nc2], r3 = rcount[nc3];
                if (nc1 === nc2 && nc2 === nc3) count += ms3(r1);
                else if (nc1 === nc2) count += ms2(r1) * r3;
                else if (nc2 === nc3) count += r1 * ms2(r2);
                else count += r1 * r2 * r3;
            }
        }
    }
    return count;
}

function countBcp(rcount, n) {
    if (n % 2 !== 0) return 0;
    const r = rcount[n >> 1];
    return ms2(r);
}

function countCcp(rcount, n) {
    const m = n - 1;
    const maxRad = m >> 1;
    let count = 0;
    for (let nc1 = 0; nc1 * 4 <= m; nc1++) {
        for (let nc2 = nc1; nc1 + nc2 * 3 <= m; nc2++) {
            const remain = m - nc1 - nc2;
            for (let nc3 = nc2; nc3 * 2 <= remain; nc3++) {
                const nc4 = remain - nc3;
                if (nc4 >= nc3 && nc4 <= maxRad) {
                    const r1 = rcount[nc1], r2 = rcount[nc2], r3 = rcount[nc3], r4 = rcount[nc4];
                    if (nc1 === nc2 && nc2 === nc3 && nc3 === nc4) count += ms4(r1);
                    else if (nc1 === nc2 && nc2 === nc3) count += ms3(r1) * r4;
                    else if (nc2 === nc3 && nc3 === nc4) count += r1 * ms3(r2);
                    else if (nc1 === nc2 && nc3 === nc4) count += ms2(r1) * ms2(r3);
                    else if (nc1 === nc2) count += ms2(r1) * r3 * r4;
                    else if (nc2 === nc3) count += r1 * ms2(r2) * r4;
                    else if (nc3 === nc4) count += r1 * r2 * ms2(r3);
                    else count += r1 * r2 * r3 * r4;
                }
            }
        }
    }
    return count;
}

function nb(n) {
    if (n < 1) return 0;
    const half = n >> 1;
    const rcount = new Int32Array(half + 1);
    rcount[0] = 1;
    for (let k = 1; k <= half; k++) {
        rcount[k] = countRadicals(rcount, k);
    }
    return countBcp(rcount, n) + countCcp(rcount, n);
}

function main() {
    let result = 0;
    for (let iter = 0; iter < 10; iter++) {
        for (let n = 1; n <= 23; n++) {
            result = nb(n);
        }
    }
    process.stdout.write("paraffins: nb(23) = " + result + "\n");
    if (result === 5731580) {
        process.stdout.write("paraffins: PASS\n");
    } else {
        process.stdout.write("paraffins: FAIL (expected 5731580)\n");
    }
}

main();

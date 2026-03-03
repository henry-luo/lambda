// Larceny Benchmark: pnpoly (Node.js)
// Point-in-polygon using ray casting
'use strict';

function pnpoly(xs, ys, n, testx, testy) {
    let inside = false;
    let j = n - 1;
    for (let i = 0; i < n; i++) {
        const yi = ys[i], yj = ys[j];
        if ((yi > testy) !== (yj > testy)) {
            const xtest = (xs[j] - xs[i]) * (testy - yi) / (yj - yi) + xs[i];
            if (testx < xtest) {
                inside = !inside;
            }
        }
        j = i;
    }
    return inside;
}

function main() {
    const xs = [0.0, 1.0, 1.0, 0.0, 0.0,
                1.0, -0.5, -1.0, -1.0, -2.0,
                -2.5, -2.0, -1.5, -0.5, 0.5,
                1.0, 0.5, 0.0, -0.5, -1.0];
    const ys = [0.0, 0.0, 1.0, 1.0, 2.0,
                3.0, 2.0, 3.0, 0.0, -0.5,
                0.5, 1.5, 2.0, 3.0, 3.0,
                2.0, 1.0, 0.5, -1.0, -1.0];
    const n = 20;

    let count = 0;
    let total = 0;
    for (let ix = 0; ix < 500; ix++) {
        const testx = -2.5 + ix * 0.008;
        for (let iy = 0; iy < 200; iy++) {
            const testy = -1.5 + iy * 0.025;
            if (pnpoly(xs, ys, n, testx, testy)) {
                count++;
            }
            total++;
        }
    }
    process.stdout.write("pnpoly: total=" + total + " inside=" + count + "\n");
    process.stdout.write("pnpoly: DONE\n");
}

main();

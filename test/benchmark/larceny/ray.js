// Larceny Benchmark: ray (Node.js)
// Simple ray tracer — cast rays against spheres
'use strict';

function sphereIntersect(ox, oy, oz, dx, dy, dz, cx, cy, cz, r) {
    const ex = ox - cx, ey = oy - cy, ez = oz - cz;
    const a = dx * dx + dy * dy + dz * dz;
    const b = 2.0 * (ex * dx + ey * dy + ez * dz);
    const c = ex * ex + ey * ey + ez * ez - r * r;
    const disc = b * b - 4.0 * a * c;
    if (disc < 0.0) return -1.0;
    let t = (-b - Math.sqrt(disc)) / (2.0 * a);
    if (t > 0.001) return t;
    t = (-b + Math.sqrt(disc)) / (2.0 * a);
    if (t > 0.001) return t;
    return -1.0;
}

function main() {
    const sx = [0.0, -2.0, 2.0, 0.0];
    const sy = [0.0, 0.0, 0.0, 2.0];
    const sz = [5.0, 5.0, 5.0, 5.0];
    const sr = [1.0, 1.0, 1.0, 1.0];
    const numSpheres = 4;
    const grid = 100;

    let hits = 0;
    for (let py = 0; py < grid; py++) {
        for (let px = 0; px < grid; px++) {
            let dx = (px - 50.0) / 50.0;
            let dy = (py - 50.0) / 50.0;
            let dz = 1.0;
            const len = Math.sqrt(dx * dx + dy * dy + dz * dz);
            dx /= len; dy /= len; dz /= len;

            let minT = 999999.0;
            for (let si = 0; si < numSpheres; si++) {
                const t = sphereIntersect(0, 0, 0, dx, dy, dz, sx[si], sy[si], sz[si], sr[si]);
                if (t > 0.0 && t < minT) minT = t;
            }
            if (minT < 999999.0) hits++;
        }
    }

    process.stdout.write("ray: hits=" + hits + "\n");
    if (hits > 0 && hits < 10000) {
        process.stdout.write("ray: PASS\n");
    } else {
        process.stdout.write("ray: FAIL\n");
    }
}

main();

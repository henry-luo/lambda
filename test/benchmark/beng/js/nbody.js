// BENG Benchmark: n-body (Node.js reference)
// Planetary motion simulation

const N = parseInt(process.argv[2] || "1000");
const PI = 3.141592653589793;
const SOLAR_MASS = 4 * PI * PI;
const DAYS_PER_YEAR = 365.24;

const bodies = [
    // Sun
    { x: 0, y: 0, z: 0, vx: 0, vy: 0, vz: 0, mass: SOLAR_MASS },
    // Jupiter
    { x: 4.84143144246472090e+00, y: -1.16032004402742839e+00, z: -1.03622044471123109e-01,
      vx: 1.66007664274403694e-03 * DAYS_PER_YEAR, vy: 7.69901118419740425e-03 * DAYS_PER_YEAR, vz: -6.90460016972063023e-05 * DAYS_PER_YEAR,
      mass: 9.54791938424326609e-04 * SOLAR_MASS },
    // Saturn
    { x: 8.34336671824457987e+00, y: 4.12479856412430479e+00, z: -4.03523417114321381e-01,
      vx: -2.76742510726862411e-03 * DAYS_PER_YEAR, vy: 4.99852801234917238e-03 * DAYS_PER_YEAR, vz: 2.30417297573763929e-05 * DAYS_PER_YEAR,
      mass: 2.85885980666130812e-04 * SOLAR_MASS },
    // Uranus
    { x: 1.28943695621391310e+01, y: -1.51111514016986312e+01, z: -2.23307578892655734e-01,
      vx: 2.96460137564761618e-03 * DAYS_PER_YEAR, vy: 2.37847173959480950e-03 * DAYS_PER_YEAR, vz: -2.96589568540237556e-05 * DAYS_PER_YEAR,
      mass: 4.36624404335156298e-05 * SOLAR_MASS },
    // Neptune
    { x: 1.53796971148509165e+01, y: -2.59193146099879641e+01, z: 1.79258772950371181e-01,
      vx: 2.68067772490389322e-03 * DAYS_PER_YEAR, vy: 1.62824170038242295e-03 * DAYS_PER_YEAR, vz: -9.51592254519715870e-05 * DAYS_PER_YEAR,
      mass: 5.15138902046611451e-05 * SOLAR_MASS }
];

function offsetMomentum() {
    let px = 0, py = 0, pz = 0;
    for (const b of bodies) {
        px += b.vx * b.mass;
        py += b.vy * b.mass;
        pz += b.vz * b.mass;
    }
    bodies[0].vx = -px / SOLAR_MASS;
    bodies[0].vy = -py / SOLAR_MASS;
    bodies[0].vz = -pz / SOLAR_MASS;
}

function energy() {
    let e = 0;
    const n = bodies.length;
    for (let i = 0; i < n; i++) {
        const bi = bodies[i];
        e += 0.5 * bi.mass * (bi.vx * bi.vx + bi.vy * bi.vy + bi.vz * bi.vz);
        for (let j = i + 1; j < n; j++) {
            const bj = bodies[j];
            const dx = bi.x - bj.x, dy = bi.y - bj.y, dz = bi.z - bj.z;
            e -= bi.mass * bj.mass / Math.sqrt(dx * dx + dy * dy + dz * dz);
        }
    }
    return e;
}

function advance(dt) {
    const n = bodies.length;
    for (let i = 0; i < n; i++) {
        const bi = bodies[i];
        for (let j = i + 1; j < n; j++) {
            const bj = bodies[j];
            const dx = bi.x - bj.x, dy = bi.y - bj.y, dz = bi.z - bj.z;
            const dist = Math.sqrt(dx * dx + dy * dy + dz * dz);
            const mag = dt / (dist * dist * dist);
            bi.vx -= dx * bj.mass * mag;
            bi.vy -= dy * bj.mass * mag;
            bi.vz -= dz * bj.mass * mag;
            bj.vx += dx * bi.mass * mag;
            bj.vy += dy * bi.mass * mag;
            bj.vz += dz * bi.mass * mag;
        }
    }
    for (const b of bodies) {
        b.x += dt * b.vx;
        b.y += dt * b.vy;
        b.z += dt * b.vz;
    }
}

function formatEnergy(e) {
    return e.toFixed(9);
}

offsetMomentum();
console.log(formatEnergy(energy()));

for (let i = 0; i < N; i++) {
    advance(0.01);
}
console.log(formatEnergy(energy()));

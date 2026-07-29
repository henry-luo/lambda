// JetStream Benchmark: n-body (SunSpider) (Typed version)
// Planetary orbit simulation — gravitational N-body problem
// Original: The Great Computer Language Shootout
// http://shootout.alioth.debian.org/
// Expected: ret == -1.3524862408537381 (across multiple sizes)

let PI = 3.141592653589793
let SOLAR_MASS = 4.0 * PI * PI
let DAYS_PER_YEAR = 365.24

// Bodies stored as SoA (struct-of-arrays) for 5 bodies:
// 0=Sun, 1=Jupiter, 2=Saturn, 3=Uranus, 4=Neptune

// The float[] annotations on the run_nbody_system locals are dropped: a float
// literal array already infers a packed ArrayNum, and the bracket annotation
// re-tags the var as ANY, losing the direct-index path on every body access
pn offset_momentum(bvx: float[], bvy: float[], bvz: float[], bmass: float[]) {
    var px: float = 0.0
    var py: float = 0.0
    var pz: float = 0.0
    var i: int = 0
    while (i < 5) {
        px = px + bvx[i] * bmass[i]
        py = py + bvy[i] * bmass[i]
        pz = pz + bvz[i] * bmass[i]
        i = i + 1
    }
    bvx[0] = 0.0 - px / SOLAR_MASS
    bvy[0] = 0.0 - py / SOLAR_MASS
    bvz[0] = 0.0 - pz / SOLAR_MASS
}

pn advance(bx: float[], by: float[], bz: float[],
           bvx: float[], bvy: float[], bvz: float[],
           bmass: float[], dt: float) {
    var i: int = 0
    while (i < 5) {
        var j: int = i + 1
        while (j < 5) {
            var dx: float = bx[i] - bx[j]
            var dy: float = by[i] - by[j]
            var dz: float = bz[i] - bz[j]
            var d_squared: float = dx * dx + dy * dy + dz * dz
            var distance: float = math.sqrt(d_squared)
            var mag: float = dt / (d_squared * distance)
            bvx[i] = bvx[i] - dx * bmass[j] * mag
            bvy[i] = bvy[i] - dy * bmass[j] * mag
            bvz[i] = bvz[i] - dz * bmass[j] * mag
            bvx[j] = bvx[j] + dx * bmass[i] * mag
            bvy[j] = bvy[j] + dy * bmass[i] * mag
            bvz[j] = bvz[j] + dz * bmass[i] * mag
            j = j + 1
        }
        i = i + 1
    }
    i = 0
    while (i < 5) {
        bx[i] = bx[i] + dt * bvx[i]
        by[i] = by[i] + dt * bvy[i]
        bz[i] = bz[i] + dt * bvz[i]
        i = i + 1
    }
}

pn energy(bx: float[], by: float[], bz: float[],
          bvx: float[], bvy: float[], bvz: float[],
          bmass: float[]) {
    var e: float = 0.0
    var i: int = 0
    while (i < 5) {
        e = e + 0.5 * bmass[i] * (bvx[i] * bvx[i] + bvy[i] * bvy[i] + bvz[i] * bvz[i])
        var j: int = i + 1
        while (j < 5) {
            var dx: float = bx[i] - bx[j]
            var dy: float = by[i] - by[j]
            var dz: float = bz[i] - bz[j]
            var distance: float = math.sqrt(dx * dx + dy * dy + dz * dz)
            e = e - (bmass[i] * bmass[j]) / distance
            j = j + 1
        }
        i = i + 1
    }
    return e
}

pn run_nbody_system(n_steps: int) {
    var bx = [0.0, 4.84143144246472090e+00, 8.34336671824457987e+00,
                        1.28943695621391310e+01, 1.53796971148509165e+01]
    var by = [0.0, -1.16032004402742839e+00, 4.12479856412430479e+00,
                        -1.51111514016986312e+01, -2.59193146099879641e+01]
    var bz = [0.0, -1.03622044471123109e-01, -4.03523417114321381e-01,
                        -2.23307578892655734e-01, 1.79258772950371181e-01]
    var bvx = [0.0, 1.66007664274403694e-03 * DAYS_PER_YEAR,
                         -2.76742510726862411e-03 * DAYS_PER_YEAR,
                         2.96460137564761618e-03 * DAYS_PER_YEAR,
                         2.68067772490389322e-03 * DAYS_PER_YEAR]
    var bvy = [0.0, 7.69901118419740425e-03 * DAYS_PER_YEAR,
                         4.99852801234917238e-03 * DAYS_PER_YEAR,
                         2.37847173959480950e-03 * DAYS_PER_YEAR,
                         1.62824170038242295e-03 * DAYS_PER_YEAR]
    var bvz = [0.0, -6.90460016972063023e-05 * DAYS_PER_YEAR,
                         2.30417297573763929e-05 * DAYS_PER_YEAR,
                         -2.96589568540237556e-05 * DAYS_PER_YEAR,
                         -9.51592254519715870e-05 * DAYS_PER_YEAR]
    var bmass = [SOLAR_MASS, 9.54791938424326609e-04 * SOLAR_MASS,
                          2.85885980666130812e-04 * SOLAR_MASS,
                          4.36624404335156298e-05 * SOLAR_MASS,
                          5.15138902046611451e-05 * SOLAR_MASS]

    offset_momentum(bvx, bvy, bvz, bmass)
    // Original JetStream: accumulate BOTH initial and final energy
    var ret: float = energy(bx, by, bz, bvx, bvy, bvz, bmass)
    var k: int = 0
    while (k < n_steps) {
        advance(bx, by, bz, bvx, bvy, bvz, bmass, 0.01)
        k = k + 1
    }
    ret = ret + energy(bx, by, bz, bvx, bvy, bvz, bmass)
    return ret
}

pn run() {
    var ret: float = 0.0
    var n: int = 3
    while (n <= 24) {
        var steps: int = n * 100
        ret = ret + run_nbody_system(steps)
        n = n * 2
    }
    return ret
}

pn main() {
    var __t0 = clock()
    // Original JetStream workload: 8 iterations of run() (4500 steps each = 36000 total)
    var pass = true
    var i: int = 0
    while (i < 8) {
        var result = run()
        var check = floor(result * -10000000.0)
        if (check != 13524862) {
            print("nbody: FAIL iteration=" ++ i ++ " check=" ++ check ++ " energy=" ++ result ++ "\n")
            pass = false
        }
        i = i + 1
    }
    var __t1 = clock()
    if (pass) {
        print("nbody: PASS\n")
    }
    print("__TIMING__:" ++ ((__t1 - __t0) * 1000.0) ++ "\n")
}

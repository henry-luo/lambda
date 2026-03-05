// BENG Benchmark: n-body
// 5-body (Sun + Jovian planets) gravitational simulation
// N=1000 expected: "-0.169075164\n-0.169087605\n"

let N = 1000
let PI = 3.141592653589793
let SOLAR_MASS = 4.0 * PI * PI
let DAYS_PER_YEAR = 365.24

// format a float to exactly 9 decimal places
pn format9(x) {
    var v = x
    var neg = ""
    if (v < 0.0) {
        neg = "-"
        v = 0.0 - v
    }
    var int_part = int(floor(v))
    var frac = v - float(int_part)
    var scaled = frac * 1000000000.0 + 0.5
    var fl = floor(scaled)
    var frac_int = int(fl)
    if (frac_int >= 1000000000) {
        int_part = int_part + 1
        frac_int = 0
    }
    var frac_str = string(frac_int)
    var pad = 9 - len(frac_str)
    var prefix = ""
    while (pad > 0) {
        prefix = prefix ++ "0"
        pad = pad - 1
    }
    frac_str = prefix ++ frac_str
    var result = neg ++ string(int_part) ++ "." ++ frac_str
    return result
}

pn advance(bx, by, bz, bvx, bvy, bvz, bmass, dt) {
    var i = 0
    while (i < 5) {
        var j = i + 1
        while (j < 5) {
            var dx = bx[i] - bx[j]
            var dy = by[i] - by[j]
            var dz = bz[i] - bz[j]
            var d_squared = dx * dx + dy * dy + dz * dz
            var distance = math.sqrt(d_squared)
            var mag = dt / (d_squared * distance)

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

pn energy(bx, by, bz, bvx, bvy, bvz, bmass) {
    var e = bx[0] - bx[0]
    var i = 0
    while (i < 5) {
        var ke = bvx[i] * bvx[i] + bvy[i] * bvy[i] + bvz[i] * bvz[i]
        e = e + 0.5 * bmass[i] * ke
        var j = i + 1
        while (j < 5) {
            var dx = bx[i] - bx[j]
            var dy = by[i] - by[j]
            var dz = bz[i] - bz[j]
            var distance = math.sqrt(dx * dx + dy * dy + dz * dz)
            e = e - (bmass[i] * bmass[j]) / distance
            j = j + 1
        }
        i = i + 1
    }
    return e
}

pn offset_momentum(bvx, bvy, bvz, bmass) {
    var px = bvx[0] - bvx[0]
    var py = bvy[0] - bvy[0]
    var pz = bvz[0] - bvz[0]
    var i = 0
    while (i < 5) {
        px = px + bvx[i] * bmass[i]
        py = py + bvy[i] * bmass[i]
        pz = pz + bvz[i] * bmass[i]
        i = i + 1
    }
    bvx[0] = 0.0 - (px / SOLAR_MASS)
    bvy[0] = 0.0 - (py / SOLAR_MASS)
    bvz[0] = 0.0 - (pz / SOLAR_MASS)
}

pn main() {
    var __t0 = clock()
    var bx = [0.0,
        4.84143144246472090e+00,
        8.34336671824457987e+00,
        1.28943695621391310e+01,
        1.53796971148509165e+01]
    var by = [0.0,
        -1.16032004402742839e+00,
        4.12479856412430479e+00,
        -1.51111514016986312e+01,
        -2.59193146099879641e+01]
    var bz = [0.0,
        -1.03622044471123109e-01,
        -4.03523417114321381e-01,
        -2.23307578892655734e-01,
        1.79258772950371181e-01]
    var bvx = [0.0,
        1.66007664274403694e-03 * DAYS_PER_YEAR,
        -2.76742510726862411e-03 * DAYS_PER_YEAR,
        2.96460137564761618e-03 * DAYS_PER_YEAR,
        2.68067772490389322e-03 * DAYS_PER_YEAR]
    var bvy = [0.0,
        7.69901118419740425e-03 * DAYS_PER_YEAR,
        4.99852801234917238e-03 * DAYS_PER_YEAR,
        2.37847173959480950e-03 * DAYS_PER_YEAR,
        1.62824170038242295e-03 * DAYS_PER_YEAR]
    var bvz = [0.0,
        -6.90460016972063023e-05 * DAYS_PER_YEAR,
        2.30417297573763929e-05 * DAYS_PER_YEAR,
        -2.96589568540237556e-05 * DAYS_PER_YEAR,
        -9.51592254519715870e-05 * DAYS_PER_YEAR]
    var bmass = [SOLAR_MASS,
        9.54791938424326609e-04 * SOLAR_MASS,
        2.85885980666130812e-04 * SOLAR_MASS,
        4.36624404335156298e-05 * SOLAR_MASS,
        5.15138902046611451e-05 * SOLAR_MASS]

    offset_momentum(bvx, bvy, bvz, bmass)
    print(format9(energy(bx, by, bz, bvx, bvy, bvz, bmass)) ++ "\n")

    var i = 0
    while (i < N) {
        advance(bx, by, bz, bvx, bvy, bvz, bmass, 0.01)
        i = i + 1
    }
    print(format9(energy(bx, by, bz, bvx, bvy, bvz, bmass)) ++ "\n")
    var __t1 = clock()
    print("__TIMING__:" ++ string((__t1 - __t0) * 1000.0) ++ "\n")
}

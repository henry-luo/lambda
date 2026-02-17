// AWFY Benchmark: NBody (Typed version)
// Expected result: PASS (energy after 1000 iterations ~ -0.169088)

let PI = 3.141592653589793
let SOLAR_MASS = 4.0 * PI * PI
let DAYS_PER_YEAR = 365.24

pn advance(bx, by, bz, bvx, bvy, bvz, bmass, dt) {
    var i: int = 0
    while (i < 5) {
        var j: int = i + 1
        while (j < 5) {
            var dx = bx[i] - bx[j]
            var dy = by[i] - by[j]
            var dz = bz[i] - bz[j]

            var d_squared = dx * dx + dy * dy + dz * dz
            var distance = sqrt(d_squared)
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
    var i: int = 0
    while (i < 5) {
        var ke = bvx[i] * bvx[i] + bvy[i] * bvy[i] + bvz[i] * bvz[i]
        e = e + 0.5 * bmass[i] * ke

        var j: int = i + 1
        while (j < 5) {
            var dx = bx[i] - bx[j]
            var dy = by[i] - by[j]
            var dz = bz[i] - bz[j]
            var distance = sqrt(dx * dx + dy * dy + dz * dz)
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
    var i: int = 0
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

pn benchmark() {
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

    var i: int = 0
    while (i < 1000) {
        advance(bx, by, bz, bvx, bvy, bvz, bmass, 0.01)
        i = i + 1
    }
    var r = energy(bx, by, bz, bvx, bvy, bvz, bmass)
    return r
}

pn main() {
    let result = benchmark()
    var check = floor(result * -10000000.0)
    if (check == 1690876) {
        print("NBody: PASS\n")
    } else {
        print("NBody: FAIL check=")
        print(check)
        print(" energy=")
        print(result)
        print("\n")
    }
}

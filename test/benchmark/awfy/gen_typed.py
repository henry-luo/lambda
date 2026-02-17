#!/usr/bin/env python3
"""Generate typed (_2.ls) versions of all AWFY benchmarks."""
import os

DIR = os.path.dirname(os.path.abspath(__file__))

benchmarks = {}

# ============================================================
# SIEVE
# ============================================================
benchmarks["sieve2.ls"] = r"""// AWFY Benchmark: Sieve of Eratosthenes (Typed version)
// Expected result: 669

pn make_array(n: int, val) {
    var arr = [val, val, val, val, val, val, val, val, val, val]
    var sz: int = 10
    while (sz * 2 <= n) {
        arr = arr ++ arr
        sz = sz * 2
    }
    if (sz < n) {
        var extra = [val]
        var esz: int = 1
        var need: int = n - sz
        while (esz * 2 <= need) {
            extra = extra ++ extra
            esz = esz * 2
        }
        if (esz < need) {
            var rest = [val]
            var rsz: int = 1
            while (rsz < need - esz) {
                rest = rest ++ [val]
                rsz = rsz + 1
            }
            extra = extra ++ rest
        }
        arr = arr ++ extra
    }
    return arr
}

pn sieve(flags, sz: int) {
    var prime_count: int = 0
    var i: int = 2
    while (i <= sz) {
        if (flags[i - 1]) {
            prime_count = prime_count + 1
            var k: int = i + i
            while (k <= sz) {
                flags[k - 1] = false
                k = k + i
            }
        }
        i = i + 1
    }
    return prime_count
}

pn benchmark() {
    var flags = make_array(5000, true)
    return sieve(flags, 5000)
}

pn main() {
    let result = benchmark()
    if (result == 669) {
        print("Sieve: PASS\n")
    } else {
        print("Sieve: FAIL result=")
        print(result)
        print("\n")
    }
}
"""

# ============================================================
# PERMUTE
# ============================================================
benchmarks["permute2.ls"] = r"""// AWFY Benchmark: Permute (Typed version)
// Expected result: 8660

pn make_array(n: int, val) {
    var arr = [val, val, val, val, val, val, val, val, val, val]
    var sz: int = 10
    while (sz * 2 <= n) {
        arr = arr ++ arr
        sz = sz * 2
    }
    if (sz < n) {
        var remain: int = n - sz
        var extra = [val]
        var esz: int = 1
        while (esz < remain) {
            extra = extra ++ [val]
            esz = esz + 1
        }
        arr = arr ++ extra
    }
    return arr
}

pn swap(v, i: int, j: int) {
    var tmp = v[i]
    v[i] = v[j]
    v[j] = tmp
}

pn permute(state, v, n: int) {
    state.count = state.count + 1
    if (n != 0) {
        var n1: int = n - 1
        permute(state, v, n1)
        var i: int = n1
        while (i >= 0) {
            swap(v, n1, i)
            permute(state, v, n1)
            swap(v, n1, i)
            i = i - 1
        }
    }
}

pn benchmark() {
    let state = {count: 0}
    var v = make_array(6, 0)
    permute(state, v, 6)
    return state.count
}

pn main() {
    let result = benchmark()
    if (result == 8660) {
        print("Permute: PASS\n")
    } else {
        print("Permute: FAIL result=")
        print(result)
        print("\n")
    }
}
"""

# ============================================================
# QUEENS
# ============================================================
benchmarks["queens2.ls"] = r"""// AWFY Benchmark: Queens (Typed version)
// Expected result: true (solved 10 times)

pn make_array(n: int, val) {
    var arr = [val, val, val, val, val, val, val, val, val, val]
    var sz: int = 10
    while (sz * 2 <= n) {
        arr = arr ++ arr
        sz = sz * 2
    }
    if (sz < n) {
        var remain: int = n - sz
        var extra = [val]
        var esz: int = 1
        while (esz < remain) {
            extra = extra ++ [val]
            esz = esz + 1
        }
        arr = arr ++ extra
    }
    return arr
}

pn get_row_column(free_rows, free_maxs, free_mins, r: int, c: int) {
    if (free_rows[r] and free_maxs[c + r] and free_mins[c - r + 7]) {
        return 1
    }
    return 0
}

pn set_row_column(free_rows, free_maxs, free_mins, r: int, c: int, v) {
    free_rows[r] = v
    free_maxs[c + r] = v
    free_mins[c - r + 7] = v
}

pn place_queen(free_rows, free_maxs, free_mins, queen_rows, c: int) {
    var r: int = 0
    while (r < 8) {
        if (get_row_column(free_rows, free_maxs, free_mins, r, c) == 1) {
            queen_rows[r] = c
            set_row_column(free_rows, free_maxs, free_mins, r, c, false)
            if (c == 7) {
                return 1
            }
            if (place_queen(free_rows, free_maxs, free_mins, queen_rows, c + 1) == 1) {
                return 1
            }
            set_row_column(free_rows, free_maxs, free_mins, r, c, true)
        }
        r = r + 1
    }
    return 0
}

pn queens() {
    var free_rows = make_array(8, true)
    var free_maxs = make_array(16, true)
    var free_mins = make_array(16, true)
    var queen_rows = make_array(8, -1)
    return place_queen(free_rows, free_maxs, free_mins, queen_rows, 0)
}

pn benchmark() {
    var result: int = 1
    var i: int = 0
    while (i < 10) {
        if (queens() != 1) {
            result = 0
        }
        i = i + 1
    }
    return result
}

pn main() {
    let result = benchmark()
    if (result == 1) {
        print("Queens: PASS\n")
    } else {
        print("Queens: FAIL\n")
    }
}
"""

# ============================================================
# TOWERS
# ============================================================
benchmarks["towers2.ls"] = r"""// AWFY Benchmark: Towers of Hanoi (Typed version)
// Expected result: 8191

pn make_array(n: int, val) {
    var arr = [val, val, val, val, val, val, val, val, val, val]
    var sz: int = 10
    while (sz * 2 <= n) {
        arr = arr ++ arr
        sz = sz * 2
    }
    if (sz < n) {
        var remain: int = n - sz
        var extra = [val]
        var esz: int = 1
        while (esz < remain) {
            extra = extra ++ [val]
            esz = esz + 1
        }
        arr = arr ++ extra
    }
    return arr
}

pn push_disk(piles, tops, disk_size: int, pile: int) {
    let t: int = tops[pile]
    piles[pile * 14 + t] = disk_size
    tops[pile] = t + 1
}

pn pop_disk_from(piles, tops, pile: int) {
    let t: int = tops[pile] - 1
    tops[pile] = t
    let disk_size = piles[pile * 14 + t]
    return disk_size
}

pn move_top_disk(piles, tops, state, from_pile: int, to_pile: int) {
    let disk = pop_disk_from(piles, tops, from_pile)
    push_disk(piles, tops, disk, to_pile)
    state.moves = state.moves + 1
}

pn move_disks(piles, tops, state, disks: int, from_pile: int, to_pile: int) {
    if (disks == 1) {
        move_top_disk(piles, tops, state, from_pile, to_pile)
        return 0
    }
    var other_pile: int = (3 - from_pile) - to_pile
    move_disks(piles, tops, state, disks - 1, from_pile, other_pile)
    move_top_disk(piles, tops, state, from_pile, to_pile)
    move_disks(piles, tops, state, disks - 1, other_pile, to_pile)
}

pn benchmark() {
    var piles = make_array(42, 0)
    var tops = [0, 0, 0]
    var i: int = 12
    while (i >= 0) {
        push_disk(piles, tops, i, 0)
        i = i - 1
    }
    let state = {moves: 0}
    move_disks(piles, tops, state, 13, 0, 1)
    return state.moves
}

pn main() {
    let result = benchmark()
    if (result == 8191) {
        print("Towers: PASS\n")
    } else {
        print("Towers: FAIL result=")
        print(result)
        print("\n")
    }
}
"""

# ============================================================
# BOUNCE
# ============================================================
benchmarks["bounce2.ls"] = r"""// AWFY Benchmark: Bounce (Typed version)
// Expected result: 1331

pn make_array(n: int, val) {
    var arr = [val, val, val, val, val, val, val, val, val, val]
    var sz: int = 10
    while (sz * 2 <= n) {
        arr = arr ++ arr
        sz = sz * 2
    }
    if (sz < n) {
        var remain: int = n - sz
        var extra = [val]
        var esz: int = 1
        while (esz < remain) {
            extra = extra ++ [val]
            esz = esz + 1
        }
        arr = arr ++ extra
    }
    return arr
}

pn random_next(seed_arr) {
    var s: int = seed_arr[0]
    s = s * 1309 + 13849
    s = s % 65536
    seed_arr[0] = s
    return s
}

pn benchmark() {
    var seed_arr = [74755]
    var ball_count: int = 100
    var bounces: int = 0

    var bx = make_array(ball_count, 0)
    var by = make_array(ball_count, 0)
    var bxv = make_array(ball_count, 0)
    var byv = make_array(ball_count, 0)

    var i: int = 0
    while (i < ball_count) {
        bx[i] = random_next(seed_arr) % 500
        by[i] = random_next(seed_arr) % 500
        bxv[i] = (random_next(seed_arr) % 300) - 150
        byv[i] = (random_next(seed_arr) % 300) - 150
        i = i + 1
    }

    i = 0
    while (i < 50) {
        var j: int = 0
        while (j < ball_count) {
            bx[j] = bx[j] + bxv[j]
            by[j] = by[j] + byv[j]

            var bounced: int = 0
            if (bx[j] > 500) {
                bx[j] = 500
                var axv: int = abs(bxv[j])
                bxv[j] = 0 - axv
                bounced = 1
            }
            if (bx[j] < 0) {
                bx[j] = 0
                var axv2: int = abs(bxv[j])
                bxv[j] = axv2
                bounced = 1
            }
            if (by[j] > 500) {
                by[j] = 500
                var ayv: int = abs(byv[j])
                byv[j] = 0 - ayv
                bounced = 1
            }
            if (by[j] < 0) {
                by[j] = 0
                var ayv2: int = abs(byv[j])
                byv[j] = ayv2
                bounced = 1
            }
            bounces = bounces + bounced
            j = j + 1
        }
        i = i + 1
    }
    return bounces
}

pn main() {
    let result = benchmark()
    if (result == 1331) {
        print("Bounce: PASS\n")
    } else {
        print("Bounce: FAIL result=")
        print(result)
        print("\n")
    }
}
"""

# ============================================================
# LIST
# ============================================================
benchmarks["list2.ls"] = r"""// AWFY Benchmark: List (Typed version)
// Expected result: 10

pn make_list(length: int) {
    if (length == 0) {
        return null
    }
    let e = {val: length, next: make_list(length - 1)}
    return e
}

pn list_length(node) {
    if (node == null) {
        return 0
    }
    return 1 + list_length(node.next)
}

pn is_shorter_than(x, y) {
    var x_tail = x
    var y_tail = y
    while (y_tail != null) {
        if (x_tail == null) {
            return 1
        }
        x_tail = x_tail.next
        y_tail = y_tail.next
    }
    return 0
}

pn tail(x, y, z) {
    if (is_shorter_than(y, x) == 1) {
        return tail(
            tail(x.next, y, z),
            tail(y.next, z, x),
            tail(z.next, x, y)
        )
    }
    return z
}

pn benchmark() {
    let result = tail(
        make_list(15),
        make_list(10),
        make_list(6)
    )
    return list_length(result)
}

pn main() {
    let result = benchmark()
    if (result == 10) {
        print("List: PASS\n")
    } else {
        print("List: FAIL result=")
        print(result)
        print("\n")
    }
}
"""

# ============================================================
# STORAGE
# ============================================================
benchmarks["storage2.ls"] = r"""// AWFY Benchmark: Storage (Typed version)
// Expected result: 5461

pn random_next(seed_arr) {
    var s: int = seed_arr[0]
    s = s * 1309 + 13849
    s = s % 65536
    seed_arr[0] = s
    return s
}

pn make_array(n: int, val) {
    var arr = [val, val, val, val, val, val, val, val, val, val]
    var sz: int = 10
    while (sz * 2 <= n) {
        arr = arr ++ arr
        sz = sz * 2
    }
    if (sz < n) {
        var remain: int = n - sz
        var extra = [val]
        var esz: int = 1
        while (esz < remain) {
            extra = extra ++ [val]
            esz = esz + 1
        }
        arr = arr ++ extra
    }
    return arr
}

pn build_tree_depth(state, depth: int, seed_arr) {
    state.count = state.count + 1
    if (depth == 1) {
        return make_array((random_next(seed_arr) % 10) + 1, 0)
    }
    var arr = [null, null, null, null]
    var i: int = 0
    while (i < 4) {
        arr[i] = build_tree_depth(state, depth - 1, seed_arr)
        i = i + 1
    }
    return arr
}

pn benchmark() {
    var seed_arr = [74755]
    let state = {count: 0}
    build_tree_depth(state, 7, seed_arr)
    return state.count
}

pn main() {
    let result = benchmark()
    if (result == 5461) {
        print("Storage: PASS\n")
    } else {
        print("Storage: FAIL result=")
        print(result)
        print("\n")
    }
}
"""

# ============================================================
# MANDELBROT
# ============================================================
benchmarks["mandelbrot2.ls"] = r"""// AWFY Benchmark: Mandelbrot (Typed version)
// Expected result: 191 (for size 500)

pn mandelbrot() {
    var sz: int = 500
    var sum: int = 0
    var byte_acc: int = 0
    var bit_num: int = 0
    var y: int = 0

    while (y < sz) {
        var ci: float = (2.0 * y / sz) - 1.0
        var x: int = 0

        while (x < sz) {
            var zrzr: float = 0.0
            var zi: float = 0.0
            var zizi: float = 0.0
            var cr: float = (2.0 * x / sz) - 1.5

            var z: int = 0
            var not_done: int = 1
            var escape: int = 0
            while (not_done == 1 and z < 50) {
                var zr: float = zrzr - zizi + cr
                zi = 2.0 * zr * zi + ci
                zrzr = zr * zr
                zizi = zi * zi
                if (zrzr + zizi > 4.0) {
                    not_done = 0
                    escape = 1
                }
                z = z + 1
            }

            byte_acc = shl(byte_acc, 1) + escape
            bit_num = bit_num + 1

            var did_flush: int = 0
            if (bit_num == 8) {
                sum = bxor(sum, byte_acc)
                byte_acc = 0
                bit_num = 0
                did_flush = 1
            }
            if (did_flush == 0) {
                if (x == sz - 1) {
                    byte_acc = shl(byte_acc, 8 - bit_num)
                    sum = bxor(sum, byte_acc)
                    byte_acc = 0
                    bit_num = 0
                }
            }
            x = x + 1
        }
        y = y + 1
    }
    return sum
}

pn benchmark() {
    var r: int = mandelbrot()
    return r
}

pn main() {
    let result = benchmark()
    if (result == 191) {
        print("Mandelbrot: PASS\n")
    } else {
        print("Mandelbrot: FAIL result=")
        print(result)
        print("\n")
    }
}
"""

# ============================================================
# NBODY
# ============================================================
benchmarks["nbody2.ls"] = r"""// AWFY Benchmark: NBody (Typed version)
// Expected result: PASS (energy after 1000 iterations ~ -0.169088)

let PI: float = 3.141592653589793
let SOLAR_MASS: float = 4.0 * PI * PI
let DAYS_PER_YEAR: float = 365.24

pn advance(bx, by, bz, bvx, bvy, bvz, bmass, dt: float) {
    var i: int = 0
    while (i < 5) {
        var j: int = i + 1
        while (j < 5) {
            var dx: float = bx[i] - bx[j]
            var dy: float = by[i] - by[j]
            var dz: float = bz[i] - bz[j]

            var d_squared: float = dx * dx + dy * dy + dz * dz
            var distance: float = sqrt(d_squared)
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

pn energy(bx, by, bz, bvx, bvy, bvz, bmass) {
    var e: float = 0.0
    var i: int = 0
    while (i < 5) {
        var ke: float = bvx[i] * bvx[i] + bvy[i] * bvy[i] + bvz[i] * bvz[i]
        e = e + 0.5 * bmass[i] * ke

        var j: int = i + 1
        while (j < 5) {
            var dx: float = bx[i] - bx[j]
            var dy: float = by[i] - by[j]
            var dz: float = bz[i] - bz[j]
            var distance: float = sqrt(dx * dx + dy * dy + dz * dz)
            e = e - (bmass[i] * bmass[j]) / distance
            j = j + 1
        }
        i = i + 1
    }
    return e
}

pn offset_momentum(bvx, bvy, bvz, bmass) {
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
    var r: float = energy(bx, by, bz, bvx, bvy, bvz, bmass)
    return r
}

pn main() {
    let result = benchmark()
    var check: int = floor(result * -10000000.0)
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
"""

for name, content in benchmarks.items():
    path = os.path.join(DIR, name)
    with open(path, 'w') as f:
        f.write(content.lstrip('\n'))
    print(f"  wrote {name}")

print(f"\nDone: {len(benchmarks)} files written")

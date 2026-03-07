// JetStream Benchmark: navier-stokes (Octane)
// 2D fluid dynamics simulation
// Original: Oliver Hunt (http://nerget.com), V8 project authors
// Solves Navier-Stokes equations on a grid

let WIDTH = 128
let HEIGHT = 128
let ROW_SIZE = WIDTH + 2
let GRID_SIZE = (WIDTH + 2) * (HEIGHT + 2)

pn add_fields(x, s, dt: float) {
    var i: int = 0
    while (i < GRID_SIZE) {
        x[i] = x[i] + dt * s[i]
        i = i + 1
    }
}

pn set_bnd(b: int, x) {
    if (b == 1) {
        var i: int = 1
        while (i <= WIDTH) {
            x[i] = x[i + ROW_SIZE]
            x[i + (HEIGHT + 1) * ROW_SIZE] = x[i + HEIGHT * ROW_SIZE]
            i = i + 1
        }
        var j: int = 1
        while (j <= HEIGHT) {
            x[j * ROW_SIZE] = 0.0 - x[1 + j * ROW_SIZE]
            x[(WIDTH + 1) + j * ROW_SIZE] = 0.0 - x[WIDTH + j * ROW_SIZE]
            j = j + 1
        }
    } else {
        if (b == 2) {
            var i: int = 1
            while (i <= WIDTH) {
                x[i] = 0.0 - x[i + ROW_SIZE]
                x[i + (HEIGHT + 1) * ROW_SIZE] = 0.0 - x[i + HEIGHT * ROW_SIZE]
                i = i + 1
            }
            var j: int = 1
            while (j <= HEIGHT) {
                x[j * ROW_SIZE] = x[1 + j * ROW_SIZE]
                x[(WIDTH + 1) + j * ROW_SIZE] = x[WIDTH + j * ROW_SIZE]
                j = j + 1
            }
        } else {
            var i: int = 1
            while (i <= WIDTH) {
                x[i] = x[i + ROW_SIZE]
                x[i + (HEIGHT + 1) * ROW_SIZE] = x[i + HEIGHT * ROW_SIZE]
                i = i + 1
            }
            var j: int = 1
            while (j <= HEIGHT) {
                x[j * ROW_SIZE] = x[1 + j * ROW_SIZE]
                x[(WIDTH + 1) + j * ROW_SIZE] = x[WIDTH + j * ROW_SIZE]
                j = j + 1
            }
        }
    }
    var max_edge = (HEIGHT + 1) * ROW_SIZE
    x[0] = 0.5 * (x[1] + x[ROW_SIZE])
    x[max_edge] = 0.5 * (x[1 + max_edge] + x[HEIGHT * ROW_SIZE])
    x[WIDTH + 1] = 0.5 * (x[WIDTH] + x[(WIDTH + 1) + ROW_SIZE])
    x[(WIDTH + 1) + max_edge] = 0.5 * (x[WIDTH + max_edge] + x[(WIDTH + 1) + HEIGHT * ROW_SIZE])
}

pn lin_solve(b: int, x, x0, a: float, c: float, iterations: int) {
    if (a == 0.0) {
        if (c == 1.0) {
            var j: int = 1
            while (j <= HEIGHT) {
                var cr = j * ROW_SIZE + 1
                var i: int = 0
                while (i < WIDTH) {
                    x[cr] = x0[cr]
                    cr = cr + 1
                    i = i + 1
                }
                j = j + 1
            }
            set_bnd(b, x)
            return 0
        }
    }
    var inv_c = 1.0 / c
    var k: int = 0
    while (k < iterations) {
        var j: int = 1
        while (j <= HEIGHT) {
            var last_row = (j - 1) * ROW_SIZE
            var current_row = j * ROW_SIZE
            var next_row = (j + 1) * ROW_SIZE
            var last_x = x[current_row]
            current_row = current_row + 1
            var i: int = 1
            while (i <= WIDTH) {
                last_x = (x0[current_row] + a * (last_x + x[current_row + 1] + x[last_row + 1] + x[next_row + 1])) * inv_c
                x[current_row] = last_x
                current_row = current_row + 1
                last_row = last_row + 1
                next_row = next_row + 1
                i = i + 1
            }
            j = j + 1
        }
        set_bnd(b, x)
        k = k + 1
    }
    return 0
}

pn diffuse(b: int, x, x0, dt: float, iterations: int) {
    var a = 0.0
    lin_solve(b, x, x0, a, 1.0 + 4.0 * a, iterations)
}

pn advect(b: int, d, d0, u, v, dt: float) {
    var w_dt0 = dt * float(WIDTH)
    var h_dt0 = dt * float(HEIGHT)
    var wp5 = float(WIDTH) + 0.5
    var hp5 = float(HEIGHT) + 0.5
    var j: int = 1
    while (j <= HEIGHT) {
        var pos = j * ROW_SIZE
        var i: int = 1
        while (i <= WIDTH) {
            pos = pos + 1
            var x = float(i) - w_dt0 * u[pos]
            var y = float(j) - h_dt0 * v[pos]
            if (x < 0.5) {
                x = 0.5
            } else {
                if (x > wp5) {
                    x = wp5
                }
            }
            var i0 = int(floor(x))
            var i1 = i0 + 1
            if (y < 0.5) {
                y = 0.5
            } else {
                if (y > hp5) {
                    y = hp5
                }
            }
            var j0 = int(floor(y))
            var j1 = j0 + 1
            var s1 = x - float(i0)
            var s0 = 1.0 - s1
            var t1 = y - float(j0)
            var t0 = 1.0 - t1
            var row1 = j0 * ROW_SIZE
            var row2 = j1 * ROW_SIZE
            d[pos] = s0 * (t0 * d0[i0 + row1] + t1 * d0[i0 + row2]) +
                     s1 * (t0 * d0[i1 + row1] + t1 * d0[i1 + row2])
            i = i + 1
        }
        j = j + 1
    }
    set_bnd(b, d)
}

pn project(u, v, p, dv, iterations: int) {
    var h = -0.5 / math.sqrt(float(WIDTH * HEIGHT))
    var j: int = 1
    while (j <= HEIGHT) {
        var row = j * ROW_SIZE
        var i: int = 1
        while (i <= WIDTH) {
            var idx = row + i
            dv[idx] = h * (u[idx + 1] - u[idx - 1] + v[idx + ROW_SIZE] - v[idx - ROW_SIZE])
            p[idx] = 0.0
            i = i + 1
        }
        j = j + 1
    }
    set_bnd(0, dv)
    set_bnd(0, p)
    lin_solve(0, p, dv, 1.0, 4.0, iterations)

    var w_scale = 0.5 * float(WIDTH)
    var h_scale = 0.5 * float(HEIGHT)
    j = 1
    while (j <= HEIGHT) {
        var row = j * ROW_SIZE
        var i: int = 1
        while (i <= WIDTH) {
            var idx = row + i
            u[idx] = u[idx] - w_scale * (p[idx + 1] - p[idx - 1])
            v[idx] = v[idx] - h_scale * (p[idx + ROW_SIZE] - p[idx - ROW_SIZE])
            i = i + 1
        }
        j = j + 1
    }
    set_bnd(1, u)
    set_bnd(2, v)
}

pn dens_step(x, x0, u, v, dt: float, iterations: int) {
    add_fields(x, x0, dt)
    diffuse(0, x0, x, dt, iterations)
    advect(0, x, x0, u, v, dt)
}

pn vel_step(u, v, u0, v0, dt: float, iterations: int) {
    add_fields(u, u0, dt)
    add_fields(v, v0, dt)
    // swap u,u0 and v,v0 by copying
    var i: int = 0
    while (i < GRID_SIZE) {
        var tmp_u = u0[i]
        u0[i] = u[i]
        u[i] = tmp_u
        var tmp_v = v0[i]
        v0[i] = v[i]
        v[i] = tmp_v
        i = i + 1
    }
    diffuse(1, u, u0, dt, iterations)
    diffuse(2, v, v0, dt, iterations)
    project(u, v, u0, v0, iterations)
    // swap again
    i = 0
    while (i < GRID_SIZE) {
        var tmp_u = u0[i]
        u0[i] = u[i]
        u[i] = tmp_u
        var tmp_v = v0[i]
        v0[i] = v[i]
        v[i] = tmp_v
        i = i + 1
    }
    advect(1, u, u0, u0, v0, dt)
    advect(2, v, v0, u0, v0, dt)
    project(u, v, u0, v0, iterations)
}

pn add_points(dens, u, v) {
    var n: int = 64
    var i: int = 1
    while (i <= n) {
        var fn = float(n)
        var idx1 = (i + 1) + (i + 1) * ROW_SIZE
        u[idx1] = fn
        v[idx1] = fn
        dens[idx1] = 5.0

        var idx2 = (i + 1) + (n - i + 1) * ROW_SIZE
        u[idx2] = 0.0 - fn
        v[idx2] = 0.0 - fn
        dens[idx2] = 20.0

        var idx3 = (128 - i + 1) + (n + i + 1) * ROW_SIZE
        u[idx3] = 0.0 - fn
        v[idx3] = 0.0 - fn
        dens[idx3] = 30.0

        i = i + 1
    }
}

pn run_navier_stokes() {
    let iterations = 20
    let dt = 0.1
    var dens = fill(GRID_SIZE, 0.0)
    var dens_prev = fill(GRID_SIZE, 0.0)
    var u = fill(GRID_SIZE, 0.0)
    var u_prev = fill(GRID_SIZE, 0.0)
    var v = fill(GRID_SIZE, 0.0)
    var v_prev = fill(GRID_SIZE, 0.0)

    var frames_till_add = 0
    var frames_between = 5

    var frame: int = 0
    while (frame < 15) {
        // clear prev fields
        var k: int = 0
        while (k < GRID_SIZE) {
            u_prev[k] = 0.0
            v_prev[k] = 0.0
            dens_prev[k] = 0.0
            k = k + 1
        }

        // UI callback: add points when timer fires
        if (frames_till_add == 0) {
            add_points(dens_prev, u_prev, v_prev)
            frames_till_add = frames_between
            frames_between = frames_between + 1
        } else {
            frames_till_add = frames_till_add - 1
        }

        vel_step(u, v, u_prev, v_prev, dt, iterations)
        dens_step(dens, dens_prev, u, v, dt, iterations)

        frame = frame + 1
    }

    // Checksum verification (like original: ~~(dens[i]*10) per-element, sum as int)
    var result: int = 0
    var ci: int = 7000
    while (ci < 7100) {
        result = result + int(dens[ci] * 10.0)
        ci = ci + 1
    }
    return result
}

pn main() {
    var __t0 = clock()
    let result = run_navier_stokes()
    var __t1 = clock()
    if (result == 77) {
        print("navier-stokes: PASS (checksum=" ++ string(result) ++ ")\n")
    } else {
        print("navier-stokes: FAIL (checksum=" ++ string(result) ++ ", expected 77)\n")
    }
    print("__TIMING__:" ++ string((__t1 - __t0) * 1000.0) ++ "\n")
}

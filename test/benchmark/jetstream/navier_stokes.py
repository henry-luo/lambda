#!/usr/bin/env python3
"""JetStream Benchmark: navier-stokes (Octane) — Python version
2D fluid dynamics simulation
Original: Oliver Hunt (http://nerget.com), V8 project authors
Solves Navier-Stokes equations on a grid
"""
import time
import math

WIDTH = 128
HEIGHT = 128
ROW_SIZE = WIDTH + 2
GRID_SIZE = (WIDTH + 2) * (HEIGHT + 2)


def add_fields(x, s, dt):
    for i in range(GRID_SIZE):
        x[i] += dt * s[i]


def set_bnd(b, x):
    if b == 1:
        for i in range(1, WIDTH + 1):
            x[i] = x[i + ROW_SIZE]
            x[i + (HEIGHT + 1) * ROW_SIZE] = x[i + HEIGHT * ROW_SIZE]
        for j in range(1, HEIGHT + 1):
            x[j * ROW_SIZE] = -x[1 + j * ROW_SIZE]
            x[(WIDTH + 1) + j * ROW_SIZE] = -x[WIDTH + j * ROW_SIZE]
    elif b == 2:
        for i in range(1, WIDTH + 1):
            x[i] = -x[i + ROW_SIZE]
            x[i + (HEIGHT + 1) * ROW_SIZE] = -x[i + HEIGHT * ROW_SIZE]
        for j in range(1, HEIGHT + 1):
            x[j * ROW_SIZE] = x[1 + j * ROW_SIZE]
            x[(WIDTH + 1) + j * ROW_SIZE] = x[WIDTH + j * ROW_SIZE]
    else:
        for i in range(1, WIDTH + 1):
            x[i] = x[i + ROW_SIZE]
            x[i + (HEIGHT + 1) * ROW_SIZE] = x[i + HEIGHT * ROW_SIZE]
        for j in range(1, HEIGHT + 1):
            x[j * ROW_SIZE] = x[1 + j * ROW_SIZE]
            x[(WIDTH + 1) + j * ROW_SIZE] = x[WIDTH + j * ROW_SIZE]

    max_edge = (HEIGHT + 1) * ROW_SIZE
    x[0] = 0.5 * (x[1] + x[ROW_SIZE])
    x[max_edge] = 0.5 * (x[1 + max_edge] + x[HEIGHT * ROW_SIZE])
    x[WIDTH + 1] = 0.5 * (x[WIDTH] + x[(WIDTH + 1) + ROW_SIZE])
    x[(WIDTH + 1) + max_edge] = 0.5 * (x[WIDTH + max_edge] + x[(WIDTH + 1) + HEIGHT * ROW_SIZE])


def lin_solve(b, x, x0, a, c, iterations):
    if a == 0.0 and c == 1.0:
        for j in range(1, HEIGHT + 1):
            cr = j * ROW_SIZE + 1
            for i in range(WIDTH):
                x[cr] = x0[cr]
                cr += 1
        set_bnd(b, x)
        return

    inv_c = 1.0 / c
    for _ in range(iterations):
        for j in range(1, HEIGHT + 1):
            last_row = (j - 1) * ROW_SIZE
            current_row = j * ROW_SIZE
            next_row = (j + 1) * ROW_SIZE
            last_x = x[current_row]
            current_row += 1
            for i in range(1, WIDTH + 1):
                last_x = (x0[current_row] + a * (last_x + x[current_row + 1] +
                          x[last_row + 1] + x[next_row + 1])) * inv_c
                x[current_row] = last_x
                current_row += 1
                last_row += 1
                next_row += 1
        set_bnd(b, x)


def diffuse(b, x, x0, dt, iterations):
    lin_solve(b, x, x0, 0.0, 1.0, iterations)


def advect(b, d, d0, u, v, dt):
    w_dt0 = dt * float(WIDTH)
    h_dt0 = dt * float(HEIGHT)
    wp5 = float(WIDTH) + 0.5
    hp5 = float(HEIGHT) + 0.5
    for j in range(1, HEIGHT + 1):
        pos = j * ROW_SIZE
        for i in range(1, WIDTH + 1):
            pos += 1
            fx = float(i) - w_dt0 * u[pos]
            fy = float(j) - h_dt0 * v[pos]
            if fx < 0.5:
                fx = 0.5
            elif fx > wp5:
                fx = wp5
            i0 = int(math.floor(fx))
            i1 = i0 + 1
            if fy < 0.5:
                fy = 0.5
            elif fy > hp5:
                fy = hp5
            j0 = int(math.floor(fy))
            j1 = j0 + 1
            s1 = fx - float(i0)
            s0 = 1.0 - s1
            t1 = fy - float(j0)
            t0 = 1.0 - t1
            row1 = j0 * ROW_SIZE
            row2 = j1 * ROW_SIZE
            d[pos] = (s0 * (t0 * d0[i0 + row1] + t1 * d0[i0 + row2]) +
                      s1 * (t0 * d0[i1 + row1] + t1 * d0[i1 + row2]))
    set_bnd(b, d)


def project(u, v, p, dv, iterations):
    h = -0.5 / math.sqrt(float(WIDTH * HEIGHT))
    for j in range(1, HEIGHT + 1):
        row = j * ROW_SIZE
        for i in range(1, WIDTH + 1):
            idx = row + i
            dv[idx] = h * (u[idx + 1] - u[idx - 1] + v[idx + ROW_SIZE] - v[idx - ROW_SIZE])
            p[idx] = 0.0
    set_bnd(0, dv)
    set_bnd(0, p)
    lin_solve(0, p, dv, 1.0, 4.0, iterations)

    w_scale = 0.5 * float(WIDTH)
    h_scale = 0.5 * float(HEIGHT)
    for j in range(1, HEIGHT + 1):
        row = j * ROW_SIZE
        for i in range(1, WIDTH + 1):
            idx = row + i
            u[idx] -= w_scale * (p[idx + 1] - p[idx - 1])
            v[idx] -= h_scale * (p[idx + ROW_SIZE] - p[idx - ROW_SIZE])
    set_bnd(1, u)
    set_bnd(2, v)


def dens_step(x, x0, u, v, dt, iterations):
    add_fields(x, x0, dt)
    diffuse(0, x0, x, dt, iterations)
    advect(0, x, x0, u, v, dt)


def vel_step(u, v, u0, v0, dt, iterations):
    add_fields(u, u0, dt)
    add_fields(v, v0, dt)
    # swap u,u0
    for i in range(GRID_SIZE):
        u[i], u0[i] = u0[i], u[i]
        v[i], v0[i] = v0[i], v[i]
    diffuse(1, u, u0, dt, iterations)
    diffuse(2, v, v0, dt, iterations)
    project(u, v, u0, v0, iterations)
    # swap again
    for i in range(GRID_SIZE):
        u[i], u0[i] = u0[i], u[i]
        v[i], v0[i] = v0[i], v[i]
    advect(1, u, u0, u0, v0, dt)
    advect(2, v, v0, u0, v0, dt)
    project(u, v, u0, v0, iterations)


def add_points(dens, u, v):
    n = 64
    for i in range(1, n + 1):
        fn = float(n)
        idx1 = (i + 1) + (i + 1) * ROW_SIZE
        u[idx1] = fn
        v[idx1] = fn
        dens[idx1] = 5.0

        idx2 = (i + 1) + (n - i + 1) * ROW_SIZE
        u[idx2] = -fn
        v[idx2] = -fn
        dens[idx2] = 20.0

        idx3 = (128 - i + 1) + (n + i + 1) * ROW_SIZE
        u[idx3] = -fn
        v[idx3] = -fn
        dens[idx3] = 30.0


def run_navier_stokes():
    iterations = 20
    dt = 0.1
    dens = [0.0] * GRID_SIZE
    dens_prev = [0.0] * GRID_SIZE
    u = [0.0] * GRID_SIZE
    u_prev = [0.0] * GRID_SIZE
    v = [0.0] * GRID_SIZE
    v_prev = [0.0] * GRID_SIZE

    frames_till_add = 0
    frames_between = 5

    for frame in range(15):
        for k in range(GRID_SIZE):
            u_prev[k] = 0.0
            v_prev[k] = 0.0
            dens_prev[k] = 0.0

        if frames_till_add == 0:
            add_points(dens_prev, u_prev, v_prev)
            frames_till_add = frames_between
            frames_between += 1
        else:
            frames_till_add -= 1

        vel_step(u, v, u_prev, v_prev, dt, iterations)
        dens_step(dens, dens_prev, u, v, dt, iterations)

    result = 0
    for ci in range(7000, 7100):
        result += int(dens[ci] * 10.0)
    return result


def main():
    t0 = time.perf_counter_ns()
    result = run_navier_stokes()
    t1 = time.perf_counter_ns()

    if result == 77:
        print(f"navier-stokes: PASS (checksum={result})")
    else:
        print(f"navier-stokes: FAIL (checksum={result}, expected 77)")
    print(f"__TIMING__:{(t1 - t0) / 1_000_000:.3f}")


if __name__ == "__main__":
    main()

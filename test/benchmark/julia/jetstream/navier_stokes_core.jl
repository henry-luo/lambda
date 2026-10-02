# port of test/benchmark/jetstream/navier_stokes.py; see ../LICENSE.md.
# JetStream Benchmark: navier-stokes (Octane) — Julia version
# 2D fluid dynamics simulation
# Original: Oliver Hunt (http://nerget.com), V8 project authors
# Solves Navier-Stokes equations on a grid
#
const WIDTH = 128
const HEIGHT = 128
const ROW_SIZE = add0(WIDTH, 2)
const GRID_SIZE = mul0(add0(WIDTH, 2), add0(HEIGHT, 2))
function add_fields(x, s, dt)
    local i
    for i in range0(GRID_SIZE)
        set0!(x, i, add0(get0(x, i), mul0(dt, get0(s, i))))
    end
    return nothing
end

function set_bnd(b, x)
    local i, j, max_edge
    if truth0(((b == 1)))
        for i in range0(1, add0(WIDTH, 1))
            set0!(x, i, get0(x, add0(i, ROW_SIZE)))
            set0!(x, add0(i, mul0(add0(HEIGHT, 1), ROW_SIZE)), get0(x, add0(i, mul0(HEIGHT, ROW_SIZE))))
        end
        for j in range0(1, add0(HEIGHT, 1))
            set0!(x, mul0(j, ROW_SIZE), -(get0(x, add0(1, mul0(j, ROW_SIZE)))))
            set0!(x, add0(add0(WIDTH, 1), mul0(j, ROW_SIZE)), -(get0(x, add0(WIDTH, mul0(j, ROW_SIZE)))))
        end
    else
        if truth0(((b == 2)))
            for i in range0(1, add0(WIDTH, 1))
                set0!(x, i, -(get0(x, add0(i, ROW_SIZE))))
                set0!(x, add0(i, mul0(add0(HEIGHT, 1), ROW_SIZE)), -(get0(x, add0(i, mul0(HEIGHT, ROW_SIZE)))))
            end
            for j in range0(1, add0(HEIGHT, 1))
                set0!(x, mul0(j, ROW_SIZE), get0(x, add0(1, mul0(j, ROW_SIZE))))
                set0!(x, add0(add0(WIDTH, 1), mul0(j, ROW_SIZE)), get0(x, add0(WIDTH, mul0(j, ROW_SIZE))))
            end
        else
            for i in range0(1, add0(WIDTH, 1))
                set0!(x, i, get0(x, add0(i, ROW_SIZE)))
                set0!(x, add0(i, mul0(add0(HEIGHT, 1), ROW_SIZE)), get0(x, add0(i, mul0(HEIGHT, ROW_SIZE))))
            end
            for j in range0(1, add0(HEIGHT, 1))
                set0!(x, mul0(j, ROW_SIZE), get0(x, add0(1, mul0(j, ROW_SIZE))))
                set0!(x, add0(add0(WIDTH, 1), mul0(j, ROW_SIZE)), get0(x, add0(WIDTH, mul0(j, ROW_SIZE))))
            end
        end
    end
    max_edge = mul0(add0(HEIGHT, 1), ROW_SIZE)
    set0!(x, 0, mul0(0.5, add0(get0(x, 1), get0(x, ROW_SIZE))))
    set0!(x, max_edge, mul0(0.5, add0(get0(x, add0(1, max_edge)), get0(x, mul0(HEIGHT, ROW_SIZE)))))
    set0!(x, add0(WIDTH, 1), mul0(0.5, add0(get0(x, WIDTH), get0(x, add0(add0(WIDTH, 1), ROW_SIZE)))))
    set0!(x, add0(add0(WIDTH, 1), max_edge), mul0(0.5, add0(get0(x, add0(WIDTH, max_edge)), get0(x, add0(add0(WIDTH, 1), mul0(HEIGHT, ROW_SIZE))))))
    return nothing
end

function lin_solve(b, x, x0, a, c, iterations)
    local _, cr, current_row, i, inv_c, j, last_row, last_x, next_row
    if truth0((let _bool_value = ((a == 0.0)); truth0(_bool_value) ? ((c == 1.0)) : _bool_value end))
        for j in range0(1, add0(HEIGHT, 1))
            cr = add0(mul0(j, ROW_SIZE), 1)
            for i in range0(WIDTH)
                set0!(x, cr, get0(x0, cr))
                cr = add0(cr, 1)
            end
        end
        set_bnd(b, x)
        return nothing
    end
    inv_c = (1.0 / c)
    for _ in range0(iterations)
        for j in range0(1, add0(HEIGHT, 1))
            last_row = mul0((j - 1), ROW_SIZE)
            current_row = mul0(j, ROW_SIZE)
            next_row = mul0(add0(j, 1), ROW_SIZE)
            last_x = get0(x, current_row)
            current_row = add0(current_row, 1)
            for i in range0(1, add0(WIDTH, 1))
                last_x = mul0(add0(get0(x0, current_row), mul0(a, add0(add0(add0(last_x, get0(x, add0(current_row, 1))), get0(x, add0(last_row, 1))), get0(x, add0(next_row, 1))))), inv_c)
                set0!(x, current_row, last_x)
                current_row = add0(current_row, 1)
                last_row = add0(last_row, 1)
                next_row = add0(next_row, 1)
            end
        end
        set_bnd(b, x)
    end
    return nothing
end

function diffuse(b, x, x0, dt, iterations)
    lin_solve(b, x, x0, 0.0, 1.0, iterations)
    return nothing
end

function advect(b, d, d0, u, v, dt)
    local fx, fy, h_dt0, hp5, i, i0, i1, j, j0, j1, pos, row1, row2, s0, s1, t0, t1, w_dt0, wp5
    w_dt0 = mul0(dt, Float64(WIDTH))
    h_dt0 = mul0(dt, Float64(HEIGHT))
    wp5 = add0(Float64(WIDTH), 0.5)
    hp5 = add0(Float64(HEIGHT), 0.5)
    for j in range0(1, add0(HEIGHT, 1))
        pos = mul0(j, ROW_SIZE)
        for i in range0(1, add0(WIDTH, 1))
            pos = add0(pos, 1)
            fx = (Float64(i) - mul0(w_dt0, get0(u, pos)))
            fy = (Float64(j) - mul0(h_dt0, get0(v, pos)))
            if truth0(((fx < 0.5)))
                fx = 0.5
            else
                if truth0(((fx > wp5)))
                    fx = wp5
                end
            end
            i0 = int0(floor(fx))
            i1 = add0(i0, 1)
            if truth0(((fy < 0.5)))
                fy = 0.5
            else
                if truth0(((fy > hp5)))
                    fy = hp5
                end
            end
            j0 = int0(floor(fy))
            j1 = add0(j0, 1)
            s1 = (fx - Float64(i0))
            s0 = (1.0 - s1)
            t1 = (fy - Float64(j0))
            t0 = (1.0 - t1)
            row1 = mul0(j0, ROW_SIZE)
            row2 = mul0(j1, ROW_SIZE)
            set0!(d, pos, add0(mul0(s0, add0(mul0(t0, get0(d0, add0(i0, row1))), mul0(t1, get0(d0, add0(i0, row2))))), mul0(s1, add0(mul0(t0, get0(d0, add0(i1, row1))), mul0(t1, get0(d0, add0(i1, row2)))))))
        end
    end
    set_bnd(b, d)
    return nothing
end

function project(u, v, p, dv, iterations)
    local h, h_scale, i, idx, j, row, w_scale
    h = (-(0.5) / sqrt(Float64(mul0(WIDTH, HEIGHT))))
    for j in range0(1, add0(HEIGHT, 1))
        row = mul0(j, ROW_SIZE)
        for i in range0(1, add0(WIDTH, 1))
            idx = add0(row, i)
            set0!(dv, idx, mul0(h, (add0((get0(u, add0(idx, 1)) - get0(u, (idx - 1))), get0(v, add0(idx, ROW_SIZE))) - get0(v, (idx - ROW_SIZE)))))
            set0!(p, idx, 0.0)
        end
    end
    set_bnd(0, dv)
    set_bnd(0, p)
    lin_solve(0, p, dv, 1.0, 4.0, iterations)
    w_scale = mul0(0.5, Float64(WIDTH))
    h_scale = mul0(0.5, Float64(HEIGHT))
    for j in range0(1, add0(HEIGHT, 1))
        row = mul0(j, ROW_SIZE)
        for i in range0(1, add0(WIDTH, 1))
            idx = add0(row, i)
            set0!(u, idx, (get0(u, idx) - mul0(w_scale, (get0(p, add0(idx, 1)) - get0(p, (idx - 1))))))
            set0!(v, idx, (get0(v, idx) - mul0(h_scale, (get0(p, add0(idx, ROW_SIZE)) - get0(p, (idx - ROW_SIZE))))))
        end
    end
    set_bnd(1, u)
    set_bnd(2, v)
    return nothing
end

function dens_step(x, x0, u, v, dt, iterations)
    add_fields(x, x0, dt)
    diffuse(0, x0, x, dt, iterations)
    advect(0, x, x0, u, v, dt)
    return nothing
end

function vel_step(u, v, u0, v0, dt, iterations)
    add_fields(u, u0, dt); add_fields(v, v0, dt)
    u, u0 = u0, u; v, v0 = v0, v
    # the zero-diffusion solver copies both components in one grid walk.
    for j in 1:HEIGHT, i in 1:WIDTH
        at = j * ROW_SIZE + i + 1
        u[at], v[at] = u0[at], v0[at]
    end
    set_bnd(1, u); set_bnd(2, v)
    project(u, v, u0, v0, iterations)
    u, u0 = u0, u; v, v0 = v0, v
    advect(1, u, u0, u0, v0, dt); advect(2, v, v0, u0, v0, dt)
    project(u, v, u0, v0, iterations)
end

function add_points(dens, u, v)
    local fn, i, idx1, idx2, idx3, n
    n = 64
    for i in range0(1, add0(n, 1))
        fn = Float64(n)
        idx1 = add0(add0(i, 1), mul0(add0(i, 1), ROW_SIZE))
        set0!(u, idx1, fn)
        set0!(v, idx1, fn)
        set0!(dens, idx1, 5.0)
        idx2 = add0(add0(i, 1), mul0(add0((n - i), 1), ROW_SIZE))
        set0!(u, idx2, -(fn))
        set0!(v, idx2, -(fn))
        set0!(dens, idx2, 20.0)
        idx3 = add0(add0((128 - i), 1), mul0(add0(add0(n, i), 1), ROW_SIZE))
        set0!(u, idx3, -(fn))
        set0!(v, idx3, -(fn))
        set0!(dens, idx3, 30.0)
    end
    return nothing
end


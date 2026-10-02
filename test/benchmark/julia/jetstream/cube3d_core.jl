function draw_cube(qv, normals, mqube)
    cur_n = mul0([0.0], 18)
    for ni in range0(6)
        nv = vmulti2(mqube, [get0(normals, mul0(ni, 4)), get0(normals, add0(mul0(ni, 4), 1)), get0(normals, add0(mul0(ni, 4), 2))])
        set0!(cur_n, mul0(ni, 3), get0(nv, 0))
        set0!(cur_n, add0(mul0(ni, 3), 1), get0(nv, 1))
        set0!(cur_n, add0(mul0(ni, 3), 2), get0(nv, 2))
    end
    line_drawn = mul0([0], 12)
    last_px = 0
    if truth0(((get0(cur_n, add0(mul0(0, 3), 2)) < 0.0)))
        if truth0(!truth0(get0(line_drawn, 0)))
            last_px = draw_line([get0(qv, 0), get0(qv, 1)], [get0(qv, 4), get0(qv, 5)], last_px)
            set0!(line_drawn, 0, 1)
        end
        if truth0(!truth0(get0(line_drawn, 1)))
            last_px = draw_line([get0(qv, 4), get0(qv, 5)], [get0(qv, 8), get0(qv, 9)], last_px)
            set0!(line_drawn, 1, 1)
        end
        if truth0(!truth0(get0(line_drawn, 2)))
            last_px = draw_line([get0(qv, 8), get0(qv, 9)], [get0(qv, 12), get0(qv, 13)], last_px)
            set0!(line_drawn, 2, 1)
        end
        if truth0(!truth0(get0(line_drawn, 3)))
            last_px = draw_line([get0(qv, 12), get0(qv, 13)], [get0(qv, 0), get0(qv, 1)], last_px)
            set0!(line_drawn, 3, 1)
        end
    end
    if truth0(((get0(cur_n, add0(mul0(1, 3), 2)) < 0.0)))
        if truth0(!truth0(get0(line_drawn, 2)))
            last_px = draw_line([get0(qv, 12), get0(qv, 13)], [get0(qv, 8), get0(qv, 9)], last_px)
            set0!(line_drawn, 2, 1)
        end
        if truth0(!truth0(get0(line_drawn, 9)))
            last_px = draw_line([get0(qv, 8), get0(qv, 9)], [get0(qv, 24), get0(qv, 25)], last_px)
            set0!(line_drawn, 9, 1)
        end
        if truth0(!truth0(get0(line_drawn, 6)))
            last_px = draw_line([get0(qv, 24), get0(qv, 25)], [get0(qv, 28), get0(qv, 29)], last_px)
            set0!(line_drawn, 6, 1)
        end
        if truth0(!truth0(get0(line_drawn, 10)))
            last_px = draw_line([get0(qv, 28), get0(qv, 29)], [get0(qv, 12), get0(qv, 13)], last_px)
            set0!(line_drawn, 10, 1)
        end
    end
    if truth0(((get0(cur_n, add0(mul0(2, 3), 2)) < 0.0)))
        if truth0(!truth0(get0(line_drawn, 4)))
            last_px = draw_line([get0(qv, 16), get0(qv, 17)], [get0(qv, 20), get0(qv, 21)], last_px)
            set0!(line_drawn, 4, 1)
        end
        if truth0(!truth0(get0(line_drawn, 5)))
            last_px = draw_line([get0(qv, 20), get0(qv, 21)], [get0(qv, 24), get0(qv, 25)], last_px)
            set0!(line_drawn, 5, 1)
        end
        if truth0(!truth0(get0(line_drawn, 6)))
            last_px = draw_line([get0(qv, 24), get0(qv, 25)], [get0(qv, 28), get0(qv, 29)], last_px)
            set0!(line_drawn, 6, 1)
        end
        if truth0(!truth0(get0(line_drawn, 7)))
            last_px = draw_line([get0(qv, 28), get0(qv, 29)], [get0(qv, 16), get0(qv, 17)], last_px)
            set0!(line_drawn, 7, 1)
        end
    end
    if truth0(((get0(cur_n, add0(mul0(3, 3), 2)) < 0.0)))
        if truth0(!truth0(get0(line_drawn, 4)))
            last_px = draw_line([get0(qv, 16), get0(qv, 17)], [get0(qv, 20), get0(qv, 21)], last_px)
            set0!(line_drawn, 4, 1)
        end
        if truth0(!truth0(get0(line_drawn, 8)))
            last_px = draw_line([get0(qv, 20), get0(qv, 21)], [get0(qv, 4), get0(qv, 5)], last_px)
            set0!(line_drawn, 8, 1)
        end
        if truth0(!truth0(get0(line_drawn, 0)))
            last_px = draw_line([get0(qv, 4), get0(qv, 5)], [get0(qv, 0), get0(qv, 1)], last_px)
            set0!(line_drawn, 0, 1)
        end
        if truth0(!truth0(get0(line_drawn, 11)))
            last_px = draw_line([get0(qv, 0), get0(qv, 1)], [get0(qv, 16), get0(qv, 17)], last_px)
            set0!(line_drawn, 11, 1)
        end
    end
    if truth0(((get0(cur_n, add0(mul0(4, 3), 2)) < 0.0)))
        if truth0(!truth0(get0(line_drawn, 11)))
            last_px = draw_line([get0(qv, 16), get0(qv, 17)], [get0(qv, 0), get0(qv, 1)], last_px)
            set0!(line_drawn, 11, 1)
        end
        if truth0(!truth0(get0(line_drawn, 3)))
            last_px = draw_line([get0(qv, 0), get0(qv, 1)], [get0(qv, 12), get0(qv, 13)], last_px)
            set0!(line_drawn, 3, 1)
        end
        if truth0(!truth0(get0(line_drawn, 10)))
            last_px = draw_line([get0(qv, 12), get0(qv, 13)], [get0(qv, 28), get0(qv, 29)], last_px)
            set0!(line_drawn, 10, 1)
        end
        if truth0(!truth0(get0(line_drawn, 7)))
            last_px = draw_line([get0(qv, 28), get0(qv, 29)], [get0(qv, 16), get0(qv, 17)], last_px)
            set0!(line_drawn, 7, 1)
        end
    end
    if truth0(((get0(cur_n, add0(mul0(5, 3), 2)) < 0.0)))
        if truth0(!truth0(get0(line_drawn, 8)))
            last_px = draw_line([get0(qv, 4), get0(qv, 5)], [get0(qv, 20), get0(qv, 21)], last_px)
            set0!(line_drawn, 8, 1)
        end
        if truth0(!truth0(get0(line_drawn, 5)))
            last_px = draw_line([get0(qv, 20), get0(qv, 21)], [get0(qv, 24), get0(qv, 25)], last_px)
            set0!(line_drawn, 5, 1)
        end
        if truth0(!truth0(get0(line_drawn, 9)))
            last_px = draw_line([get0(qv, 24), get0(qv, 25)], [get0(qv, 8), get0(qv, 9)], last_px)
            set0!(line_drawn, 9, 1)
        end
        if truth0(!truth0(get0(line_drawn, 1)))
            last_px = draw_line([get0(qv, 8), get0(qv, 9)], [get0(qv, 4), get0(qv, 5)], last_px)
            set0!(line_drawn, 1, 1)
        end
    end
    return last_px
end

# port of test/benchmark/jetstream/cube3d.py; see ../LICENSE.md.
# JetStream Benchmark: 3d-cube (SunSpider) — Julia version
# 3D Cube Rotation — matrix transforms, line drawing, normal calculation
# Original: http://www.speich.net/computer/moztesting/3d.htm by Simon Speich
#
function mat4_identity()
    return [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0]
end

function mat4_mul(m1, m2)
    local i, j, m
    m = mul0([0.0], 16)
    for i in range0(4)
        for j in range0(4)
            set0!(m, add0(mul0(i, 4), j), add0(add0(add0(mul0(get0(m1, add0(mul0(i, 4), 0)), get0(m2, add0(mul0(0, 4), j))), mul0(get0(m1, add0(mul0(i, 4), 1)), get0(m2, add0(mul0(1, 4), j)))), mul0(get0(m1, add0(mul0(i, 4), 2)), get0(m2, add0(mul0(2, 4), j)))), mul0(get0(m1, add0(mul0(i, 4), 3)), get0(m2, add0(mul0(3, 4), j)))))
        end
    end
    return m
end

function vmulti(m, v)
    local i, r
    r = mul0([0.0], 4)
    for i in range0(4)
        set0!(r, i, add0(add0(add0(mul0(get0(m, add0(mul0(i, 4), 0)), get0(v, 0)), mul0(get0(m, add0(mul0(i, 4), 1)), get0(v, 1))), mul0(get0(m, add0(mul0(i, 4), 2)), get0(v, 2))), mul0(get0(m, add0(mul0(i, 4), 3)), get0(v, 3))))
    end
    return r
end

function vmulti2(m, v)
    local i, r
    r = mul0([0.0], 3)
    for i in range0(3)
        set0!(r, i, add0(add0(mul0(get0(m, add0(mul0(i, 4), 0)), get0(v, 0)), mul0(get0(m, add0(mul0(i, 4), 1)), get0(v, 1))), mul0(get0(m, add0(mul0(i, 4), 2)), get0(v, 2))))
    end
    return r
end

function translate_mat(m, dx, dy, dz)
    local t
    t = [1.0, 0.0, 0.0, dx, 0.0, 1.0, 0.0, dy, 0.0, 0.0, 1.0, dz, 0.0, 0.0, 0.0, 1.0]
    return mat4_mul(t, m)
end

function rotate_x(m, phi)
    local a, c, r, s
    a = (mul0(phi, pi) / 180.0)
    c = cos(a)
    s = sin(a)
    r = [1.0, 0.0, 0.0, 0.0, 0.0, c, -(s), 0.0, 0.0, s, c, 0.0, 0.0, 0.0, 0.0, 1.0]
    return mat4_mul(r, m)
end

function rotate_y(m, phi)
    local a, c, r, s
    a = (mul0(phi, pi) / 180.0)
    c = cos(a)
    s = sin(a)
    r = [c, 0.0, s, 0.0, 0.0, 1.0, 0.0, 0.0, -(s), 0.0, c, 0.0, 0.0, 0.0, 0.0, 1.0]
    return mat4_mul(r, m)
end

function rotate_z(m, phi)
    local a, c, r, s
    a = (mul0(phi, pi) / 180.0)
    c = cos(a)
    s = sin(a)
    r = [c, -(s), 0.0, 0.0, s, c, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0]
    return mat4_mul(r, m)
end

function calc_cross(v0, v1)
    return [(mul0(get0(v0, 1), get0(v1, 2)) - mul0(get0(v0, 2), get0(v1, 1))), (mul0(get0(v0, 2), get0(v1, 0)) - mul0(get0(v0, 0), get0(v1, 2))), (mul0(get0(v0, 0), get0(v1, 1)) - mul0(get0(v0, 1), get0(v1, 0)))]
end

function calc_normal(v0, v1, v2)
    local a, b, cross, length
    a = [(get0(v0, 0) - get0(v1, 0)), (get0(v0, 1) - get0(v1, 1)), (get0(v0, 2) - get0(v1, 2))]
    b = [(get0(v2, 0) - get0(v1, 0)), (get0(v2, 1) - get0(v1, 1)), (get0(v2, 2) - get0(v1, 2))]
    cross = calc_cross(a, b)
    length = sqrt(add0(add0(mul0(get0(cross, 0), get0(cross, 0)), mul0(get0(cross, 1), get0(cross, 1))), mul0(get0(cross, 2), get0(cross, 2))))
    return [(get0(cross, 0) / length), (get0(cross, 1) / length), (get0(cross, 2) / length), 1.0]
end

function draw_line(from_v, to_v, last_px)
    x, y = from_v[1], from_v[2]
    dx, dy = abs(to_v[1] - x), abs(to_v[2] - y)
    incx1 = incx2 = to_v[1] >= x ? 1 : -1
    incy1 = incy2 = to_v[2] >= y ? 1 : -1
    if dx >= dy
        incx1, incy2, den, num, add, pixels = 0, 0, dx, dx / 2, dy, dx
    else
        incx2, incy1, den, num, add, pixels = 0, 0, dy, dy / 2, dx, dy
    end
    pixels = floor(Int, last_px + pixels + 0.5)
    for _ in last_px:pixels-1
        num += add
        if num >= den
            num -= den
            x += incx1; y += incy1
        end
        x += incx2; y += incy2
    end
    return pixels
end

function run_cube(cube_size)
    local center_v, cs, cur_n, e0, e1, e2, edges, fi, last_px, line_drawn, loop_count, mqube, mtrans, n, new2, new_v, ni, normals, nv, old2, old_v, origin, pi, qv, total
    cs = Float64(cube_size)
    qv = mul0([0.0], 36)
    set0!(qv, 0, -(cs))
    set0!(qv, 1, -(cs))
    set0!(qv, 2, cs)
    set0!(qv, 3, 1.0)
    set0!(qv, 4, -(cs))
    set0!(qv, 5, cs)
    set0!(qv, 6, cs)
    set0!(qv, 7, 1.0)
    set0!(qv, 8, cs)
    set0!(qv, 9, cs)
    set0!(qv, 10, cs)
    set0!(qv, 11, 1.0)
    set0!(qv, 12, cs)
    set0!(qv, 13, -(cs))
    set0!(qv, 14, cs)
    set0!(qv, 15, 1.0)
    set0!(qv, 16, -(cs))
    set0!(qv, 17, -(cs))
    set0!(qv, 18, -(cs))
    set0!(qv, 19, 1.0)
    set0!(qv, 20, -(cs))
    set0!(qv, 21, cs)
    set0!(qv, 22, -(cs))
    set0!(qv, 23, 1.0)
    set0!(qv, 24, cs)
    set0!(qv, 25, cs)
    set0!(qv, 26, -(cs))
    set0!(qv, 27, 1.0)
    set0!(qv, 28, cs)
    set0!(qv, 29, -(cs))
    set0!(qv, 30, -(cs))
    set0!(qv, 31, 1.0)
    set0!(qv, 32, 0.0)
    set0!(qv, 33, 0.0)
    set0!(qv, 34, 0.0)
    set0!(qv, 35, 1.0)
    edges = [0, 1, 2, 3, 2, 6, 7, 6, 5, 4, 5, 1, 4, 0, 3, 1, 5, 6]
    normals = mul0([0.0], 24)
    for fi in range0(6)
        e0 = get0(edges, mul0(fi, 3))
        e1 = get0(edges, add0(mul0(fi, 3), 1))
        e2 = get0(edges, add0(mul0(fi, 3), 2))
        n = calc_normal([get0(qv, mul0(e0, 4)), get0(qv, add0(mul0(e0, 4), 1)), get0(qv, add0(mul0(e0, 4), 2))], [get0(qv, mul0(e1, 4)), get0(qv, add0(mul0(e1, 4), 1)), get0(qv, add0(mul0(e1, 4), 2))], [get0(qv, mul0(e2, 4)), get0(qv, add0(mul0(e2, 4), 1)), get0(qv, add0(mul0(e2, 4), 2))])
        set0!(normals, mul0(fi, 4), get0(n, 0))
        set0!(normals, add0(mul0(fi, 4), 1), get0(n, 1))
        set0!(normals, add0(mul0(fi, 4), 2), get0(n, 2))
        set0!(normals, add0(mul0(fi, 4), 3), get0(n, 3))
    end
    mqube = mat4_identity()
    origin = [150.0, 150.0, 20.0, 1.0]
    mtrans = translate_mat(mat4_identity(), get0(origin, 0), get0(origin, 1), get0(origin, 2))
    mqube = mat4_mul(mtrans, mqube)
    for pi in range0(9)
        old_v = [get0(qv, mul0(pi, 4)), get0(qv, add0(mul0(pi, 4), 1)), get0(qv, add0(mul0(pi, 4), 2)), get0(qv, add0(mul0(pi, 4), 3))]
        new_v = vmulti(mtrans, old_v)
        set0!(qv, mul0(pi, 4), get0(new_v, 0))
        set0!(qv, add0(mul0(pi, 4), 1), get0(new_v, 1))
        set0!(qv, add0(mul0(pi, 4), 2), get0(new_v, 2))
        set0!(qv, add0(mul0(pi, 4), 3), get0(new_v, 3))
    end
    line_pixels = [[0.0, 0.0, 0.0, 1.0] for _ in 1:18*cube_size]
    draw_cube(qv, normals, mqube)
    for loop_count in range0(51)
        center_v = [get0(qv, 32), get0(qv, 33), get0(qv, 34), get0(qv, 35)]
        mtrans = translate_mat(mat4_identity(), -(get0(center_v, 0)), -(get0(center_v, 1)), -(get0(center_v, 2)))
        mtrans = rotate_x(mtrans, 1.0)
        mtrans = rotate_y(mtrans, 3.0)
        mtrans = rotate_z(mtrans, 5.0)
        mtrans = translate_mat(mtrans, get0(center_v, 0), get0(center_v, 1), get0(center_v, 2))
        mqube = mat4_mul(mtrans, mqube)
        for pi in range0(8, -(1), -(1))
            old2 = [get0(qv, mul0(pi, 4)), get0(qv, add0(mul0(pi, 4), 1)), get0(qv, add0(mul0(pi, 4), 2)), get0(qv, add0(mul0(pi, 4), 3))]
            new2 = vmulti(mtrans, old2)
            set0!(qv, mul0(pi, 4), get0(new2, 0))
            set0!(qv, add0(mul0(pi, 4), 1), get0(new2, 1))
            set0!(qv, add0(mul0(pi, 4), 2), get0(new2, 2))
            set0!(qv, add0(mul0(pi, 4), 3), get0(new2, 3))
        end
        draw_cube(qv, normals, mqube)
    end
    total = 0.0
    for pi in range0(9)
        for component in 0:3
            total += get0(qv, pi * 4 + component)
        end
    end
    return total
end

function run()
    local expected, pass_all, sz, total
    pass_all = true
    sz = 20
    while truth0(((sz <= 160)))
        total = run_cube(sz)
        expected = 2889.0
        if truth0(((abs((total - expected)) > 5e-10)))
            println(string("3d-cube: FAIL for size=", string(sz), " sum=", string(total)))
            pass_all = false
        end
        sz = mul0(sz, 2)
    end
    return pass_all
end


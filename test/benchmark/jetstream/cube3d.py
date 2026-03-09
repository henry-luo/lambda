#!/usr/bin/env python3
"""JetStream Benchmark: 3d-cube (SunSpider) — Python version
3D Cube Rotation — matrix transforms, line drawing, normal calculation
Original: http://www.speich.net/computer/moztesting/3d.htm by Simon Speich
"""
import time
import math


def mat4_identity():
    return [1.0, 0.0, 0.0, 0.0,
            0.0, 1.0, 0.0, 0.0,
            0.0, 0.0, 1.0, 0.0,
            0.0, 0.0, 0.0, 1.0]


def mat4_mul(m1, m2):
    m = [0.0] * 16
    for i in range(4):
        for j in range(4):
            m[i * 4 + j] = (m1[i * 4 + 0] * m2[0 * 4 + j] +
                             m1[i * 4 + 1] * m2[1 * 4 + j] +
                             m1[i * 4 + 2] * m2[2 * 4 + j] +
                             m1[i * 4 + 3] * m2[3 * 4 + j])
    return m


def vmulti(m, v):
    r = [0.0] * 4
    for i in range(4):
        r[i] = (m[i * 4 + 0] * v[0] + m[i * 4 + 1] * v[1] +
                m[i * 4 + 2] * v[2] + m[i * 4 + 3] * v[3])
    return r


def vmulti2(m, v):
    r = [0.0] * 3
    for i in range(3):
        r[i] = m[i * 4 + 0] * v[0] + m[i * 4 + 1] * v[1] + m[i * 4 + 2] * v[2]
    return r


def translate_mat(m, dx, dy, dz):
    t = [1.0, 0.0, 0.0, dx,
         0.0, 1.0, 0.0, dy,
         0.0, 0.0, 1.0, dz,
         0.0, 0.0, 0.0, 1.0]
    return mat4_mul(t, m)


def rotate_x(m, phi):
    a = phi * math.pi / 180.0
    c = math.cos(a)
    s = math.sin(a)
    r = [1.0, 0.0,  0.0, 0.0,
         0.0,   c,   -s, 0.0,
         0.0,   s,    c, 0.0,
         0.0, 0.0,  0.0, 1.0]
    return mat4_mul(r, m)


def rotate_y(m, phi):
    a = phi * math.pi / 180.0
    c = math.cos(a)
    s = math.sin(a)
    r = [  c, 0.0, s, 0.0,
         0.0, 1.0, 0.0, 0.0,
          -s, 0.0, c, 0.0,
         0.0, 0.0, 0.0, 1.0]
    return mat4_mul(r, m)


def rotate_z(m, phi):
    a = phi * math.pi / 180.0
    c = math.cos(a)
    s = math.sin(a)
    r = [  c,  -s, 0.0, 0.0,
           s,   c, 0.0, 0.0,
         0.0, 0.0, 1.0, 0.0,
         0.0, 0.0, 0.0, 1.0]
    return mat4_mul(r, m)


def calc_cross(v0, v1):
    return [v0[1] * v1[2] - v0[2] * v1[1],
            v0[2] * v1[0] - v0[0] * v1[2],
            v0[0] * v1[1] - v0[1] * v1[0]]


def calc_normal(v0, v1, v2):
    a = [v0[0] - v1[0], v0[1] - v1[1], v0[2] - v1[2]]
    b = [v2[0] - v1[0], v2[1] - v1[1], v2[2] - v1[2]]
    cross = calc_cross(a, b)
    length = math.sqrt(cross[0] * cross[0] + cross[1] * cross[1] + cross[2] * cross[2])
    return [cross[0] / length, cross[1] / length, cross[2] / length, 1.0]


def draw_line(from_v, to_v, last_px):
    dx = abs(to_v[0] - from_v[0])
    dy = abs(to_v[1] - from_v[1])
    num_pix = dx if dx >= dy else dy
    return int(round(last_px + num_pix))


def run_cube(cube_size):
    cs = float(cube_size)

    # 9 points × 4 components
    qv = [0.0] * 36
    # P0: -cs,-cs,cs
    qv[0] = -cs;  qv[1] = -cs;  qv[2] = cs;  qv[3] = 1.0
    # P1: -cs,cs,cs
    qv[4] = -cs;  qv[5] = cs;   qv[6] = cs;  qv[7] = 1.0
    # P2: cs,cs,cs
    qv[8] = cs;   qv[9] = cs;   qv[10] = cs; qv[11] = 1.0
    # P3: cs,-cs,cs
    qv[12] = cs;  qv[13] = -cs; qv[14] = cs; qv[15] = 1.0
    # P4: -cs,-cs,-cs
    qv[16] = -cs; qv[17] = -cs; qv[18] = -cs; qv[19] = 1.0
    # P5: -cs,cs,-cs
    qv[20] = -cs; qv[21] = cs;  qv[22] = -cs; qv[23] = 1.0
    # P6: cs,cs,-cs
    qv[24] = cs;  qv[25] = cs;  qv[26] = -cs; qv[27] = 1.0
    # P7: cs,-cs,-cs
    qv[28] = cs;  qv[29] = -cs; qv[30] = -cs; qv[31] = 1.0
    # P8: center
    qv[32] = 0.0; qv[33] = 0.0; qv[34] = 0.0; qv[35] = 1.0

    edges = [0, 1, 2,  3, 2, 6,  7, 6, 5,  4, 5, 1,  4, 0, 3,  1, 5, 6]

    normals = [0.0] * 24
    for fi in range(6):
        e0 = edges[fi * 3]
        e1 = edges[fi * 3 + 1]
        e2 = edges[fi * 3 + 2]
        n = calc_normal(
            [qv[e0 * 4], qv[e0 * 4 + 1], qv[e0 * 4 + 2]],
            [qv[e1 * 4], qv[e1 * 4 + 1], qv[e1 * 4 + 2]],
            [qv[e2 * 4], qv[e2 * 4 + 1], qv[e2 * 4 + 2]])
        normals[fi * 4] = n[0]
        normals[fi * 4 + 1] = n[1]
        normals[fi * 4 + 2] = n[2]
        normals[fi * 4 + 3] = n[3]

    mqube = mat4_identity()
    origin = [150.0, 150.0, 20.0, 1.0]
    mtrans = translate_mat(mat4_identity(), origin[0], origin[1], origin[2])
    mqube = mat4_mul(mtrans, mqube)

    for pi in range(9):
        old_v = [qv[pi * 4], qv[pi * 4 + 1], qv[pi * 4 + 2], qv[pi * 4 + 3]]
        new_v = vmulti(mtrans, old_v)
        qv[pi * 4] = new_v[0]
        qv[pi * 4 + 1] = new_v[1]
        qv[pi * 4 + 2] = new_v[2]
        qv[pi * 4 + 3] = new_v[3]

    last_px = 0
    for loop_count in range(51):
        center_v = [qv[32], qv[33], qv[34], qv[35]]
        mtrans = translate_mat(mat4_identity(), -center_v[0], -center_v[1], -center_v[2])
        mtrans = rotate_x(mtrans, 1.0)
        mtrans = rotate_y(mtrans, 3.0)
        mtrans = rotate_z(mtrans, 5.0)
        mtrans = translate_mat(mtrans, center_v[0], center_v[1], center_v[2])
        mqube = mat4_mul(mtrans, mqube)

        for pi in range(8, -1, -1):
            old2 = [qv[pi * 4], qv[pi * 4 + 1], qv[pi * 4 + 2], qv[pi * 4 + 3]]
            new2 = vmulti(mtrans, old2)
            qv[pi * 4] = new2[0]
            qv[pi * 4 + 1] = new2[1]
            qv[pi * 4 + 2] = new2[2]
            qv[pi * 4 + 3] = new2[3]

        cur_n = [0.0] * 18
        for ni in range(6):
            nv = vmulti2(mqube, [normals[ni * 4], normals[ni * 4 + 1], normals[ni * 4 + 2]])
            cur_n[ni * 3] = nv[0]
            cur_n[ni * 3 + 1] = nv[1]
            cur_n[ni * 3 + 2] = nv[2]

        line_drawn = [0] * 12
        last_px = 0

        # Face 0: vertices 0,1,2,3 (lines 0,1,2,3)
        if cur_n[0 * 3 + 2] < 0.0:
            if not line_drawn[0]:
                last_px = draw_line([qv[0], qv[1]], [qv[4], qv[5]], last_px); line_drawn[0] = 1
            if not line_drawn[1]:
                last_px = draw_line([qv[4], qv[5]], [qv[8], qv[9]], last_px); line_drawn[1] = 1
            if not line_drawn[2]:
                last_px = draw_line([qv[8], qv[9]], [qv[12], qv[13]], last_px); line_drawn[2] = 1
            if not line_drawn[3]:
                last_px = draw_line([qv[12], qv[13]], [qv[0], qv[1]], last_px); line_drawn[3] = 1

        # Face 1: vertices 3,2,6,7 (lines 2,9,6,10)
        if cur_n[1 * 3 + 2] < 0.0:
            if not line_drawn[2]:
                last_px = draw_line([qv[12], qv[13]], [qv[8], qv[9]], last_px); line_drawn[2] = 1
            if not line_drawn[9]:
                last_px = draw_line([qv[8], qv[9]], [qv[24], qv[25]], last_px); line_drawn[9] = 1
            if not line_drawn[6]:
                last_px = draw_line([qv[24], qv[25]], [qv[28], qv[29]], last_px); line_drawn[6] = 1
            if not line_drawn[10]:
                last_px = draw_line([qv[28], qv[29]], [qv[12], qv[13]], last_px); line_drawn[10] = 1

        # Face 2: vertices 4,5,6,7 (lines 4,5,6,7)
        if cur_n[2 * 3 + 2] < 0.0:
            if not line_drawn[4]:
                last_px = draw_line([qv[16], qv[17]], [qv[20], qv[21]], last_px); line_drawn[4] = 1
            if not line_drawn[5]:
                last_px = draw_line([qv[20], qv[21]], [qv[24], qv[25]], last_px); line_drawn[5] = 1
            if not line_drawn[6]:
                last_px = draw_line([qv[24], qv[25]], [qv[28], qv[29]], last_px); line_drawn[6] = 1
            if not line_drawn[7]:
                last_px = draw_line([qv[28], qv[29]], [qv[16], qv[17]], last_px); line_drawn[7] = 1

        # Face 3: vertices 4,5,1,0 (lines 4,8,0,11)
        if cur_n[3 * 3 + 2] < 0.0:
            if not line_drawn[4]:
                last_px = draw_line([qv[16], qv[17]], [qv[20], qv[21]], last_px); line_drawn[4] = 1
            if not line_drawn[8]:
                last_px = draw_line([qv[20], qv[21]], [qv[4], qv[5]], last_px); line_drawn[8] = 1
            if not line_drawn[0]:
                last_px = draw_line([qv[4], qv[5]], [qv[0], qv[1]], last_px); line_drawn[0] = 1
            if not line_drawn[11]:
                last_px = draw_line([qv[0], qv[1]], [qv[16], qv[17]], last_px); line_drawn[11] = 1

        # Face 4: vertices 4,0,3,7 (lines 11,3,10,7)
        if cur_n[4 * 3 + 2] < 0.0:
            if not line_drawn[11]:
                last_px = draw_line([qv[16], qv[17]], [qv[0], qv[1]], last_px); line_drawn[11] = 1
            if not line_drawn[3]:
                last_px = draw_line([qv[0], qv[1]], [qv[12], qv[13]], last_px); line_drawn[3] = 1
            if not line_drawn[10]:
                last_px = draw_line([qv[12], qv[13]], [qv[28], qv[29]], last_px); line_drawn[10] = 1
            if not line_drawn[7]:
                last_px = draw_line([qv[28], qv[29]], [qv[16], qv[17]], last_px); line_drawn[7] = 1

        # Face 5: vertices 1,5,6,2 (lines 8,5,9,1)
        if cur_n[5 * 3 + 2] < 0.0:
            if not line_drawn[8]:
                last_px = draw_line([qv[4], qv[5]], [qv[20], qv[21]], last_px); line_drawn[8] = 1
            if not line_drawn[5]:
                last_px = draw_line([qv[20], qv[21]], [qv[24], qv[25]], last_px); line_drawn[5] = 1
            if not line_drawn[9]:
                last_px = draw_line([qv[24], qv[25]], [qv[8], qv[9]], last_px); line_drawn[9] = 1
            if not line_drawn[1]:
                last_px = draw_line([qv[8], qv[9]], [qv[4], qv[5]], last_px); line_drawn[1] = 1

    # Sum all vertex components for verification
    total = 0.0
    for pi in range(9):
        total += qv[pi * 4] + qv[pi * 4 + 1] + qv[pi * 4 + 2] + qv[pi * 4 + 3]
    return total


def run():
    pass_all = True
    sz = 20
    while sz <= 160:
        total = run_cube(sz)
        expected = 2889.0
        if abs(total - expected) > 0.001:
            print(f"3d-cube: FAIL for size={sz} sum={total}")
            pass_all = False
        sz *= 2
    return pass_all


def main():
    t0 = time.perf_counter_ns()
    pass_all = True
    for _ in range(8):
        if not run():
            pass_all = False
    t1 = time.perf_counter_ns()

    if pass_all:
        print("3d-cube: PASS")
    else:
        print("3d-cube: FAIL")
    print(f"__TIMING__:{(t1 - t0) / 1_000_000:.3f}")


if __name__ == "__main__":
    main()

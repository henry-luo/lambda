// JetStream Benchmark: 3d-cube (SunSpider)
// 3D Cube Rotation — matrix transforms, line drawing, normal calculation
// Original: http://www.speich.net/computer/moztesting/3d.htm by Simon Speich

// Flat 4x4 matrix stored as 16-element array [row-major]
// M[i][j] -> m[i*4+j]

pn mat4_identity() {
    return [1.0,0.0,0.0,0.0,
            0.0,1.0,0.0,0.0,
            0.0,0.0,1.0,0.0,
            0.0,0.0,0.0,1.0]
}

pn mat4_mul(m1, m2) {
    var m = fill(16, 0.0)
    var i: int = 0
    while (i < 4) {
        var j: int = 0
        while (j < 4) {
            m[i * 4 + j] = m1[i * 4 + 0] * m2[0 * 4 + j] +
                            m1[i * 4 + 1] * m2[1 * 4 + j] +
                            m1[i * 4 + 2] * m2[2 * 4 + j] +
                            m1[i * 4 + 3] * m2[3 * 4 + j]
            j = j + 1
        }
        i = i + 1
    }
    return m
}

pn mat4_add(m1, m2) {
    var m = fill(16, 0.0)
    var i: int = 0
    while (i < 16) {
        m[i] = m1[i] + m2[i]
        i = i + 1
    }
    return m
}

// multiply 4x4 matrix with 4-element vector
pn vmulti(m, v) {
    var r = fill(4, 0.0)
    var i: int = 0
    while (i < 4) {
        r[i] = m[i * 4 + 0] * v[0] + m[i * 4 + 1] * v[1] +
               m[i * 4 + 2] * v[2] + m[i * 4 + 3] * v[3]
        i = i + 1
    }
    return r
}

// multiply 3x3 sub-matrix with 3-element vector
pn vmulti2(m, v) {
    var r = fill(3, 0.0)
    var i: int = 0
    while (i < 3) {
        r[i] = m[i * 4 + 0] * v[0] + m[i * 4 + 1] * v[1] + m[i * 4 + 2] * v[2]
        i = i + 1
    }
    return r
}

pn translate_mat(m, dx: float, dy: float, dz: float) {
    var t = [1.0,0.0,0.0,dx,
             0.0,1.0,0.0,dy,
             0.0,0.0,1.0,dz,
             0.0,0.0,0.0,1.0]
    return mat4_mul(t, m)
}

pn rotate_x(m, phi: float) {
    var a = phi * math.pi / 180.0
    var c = math.cos(a)
    var s = math.sin(a)
    var r = [1.0, 0.0,  0.0, 0.0,
             0.0,   c, 0.0 - s, 0.0,
             0.0,   s,    c, 0.0,
             0.0, 0.0,  0.0, 1.0]
    return mat4_mul(r, m)
}

pn rotate_y(m, phi: float) {
    var a = phi * math.pi / 180.0
    var c = math.cos(a)
    var s = math.sin(a)
    var r = [  c, 0.0, s, 0.0,
             0.0, 1.0, 0.0, 0.0,
             0.0 - s, 0.0, c, 0.0,
             0.0, 0.0, 0.0, 1.0]
    return mat4_mul(r, m)
}

pn rotate_z(m, phi: float) {
    var a = phi * math.pi / 180.0
    var c = math.cos(a)
    var s = math.sin(a)
    var r = [  c, 0.0 - s, 0.0, 0.0,
               s,    c, 0.0, 0.0,
             0.0,  0.0, 1.0, 0.0,
             0.0,  0.0, 0.0, 1.0]
    return mat4_mul(r, m)
}

pn calc_cross(v0, v1) {
    return [v0[1] * v1[2] - v0[2] * v1[1],
            v0[2] * v1[0] - v0[0] * v1[2],
            v0[0] * v1[1] - v0[1] * v1[0]]
}

pn calc_normal(v0, v1, v2) {
    var a = [v0[0] - v1[0], v0[1] - v1[1], v0[2] - v1[2]]
    var b = [v2[0] - v1[0], v2[1] - v1[1], v2[2] - v1[2]]
    var cross = calc_cross(a, b)
    var length = math.sqrt(cross[0] * cross[0] + cross[1] * cross[1] + cross[2] * cross[2])
    return [cross[0] / length, cross[1] / length, cross[2] / length, 1.0]
}

// Draw line (just count pixels, no actual rendering)
pn draw_line(from_v, to_v, last_px: int) {
    var x1 = from_v[0]
    var x2 = to_v[0]
    var y1 = from_v[1]
    var y2 = to_v[1]
    var dx = abs(x2 - x1)
    var dy = abs(y2 - y1)
    var num_pix = 0.0
    if (dx >= dy) {
        num_pix = dx
    } else {
        num_pix = dy
    }
    return int(round(float(last_px) + num_pix))
}

pn run_cube(cube_size: int) {
    let cs = float(cube_size)

    // Qube vertices as flat array: 9 points x 4 components = 36
    // q[i] = [x, y, z, 1]
    var qv = fill(36, 0.0)
    // P0: -cs,-cs,cs
    qv[0] = 0.0 - cs
    qv[1] = 0.0 - cs
    qv[2] = cs
    qv[3] = 1.0
    // P1: -cs,cs,cs
    qv[4] = 0.0 - cs
    qv[5] = cs
    qv[6] = cs
    qv[7] = 1.0
    // P2: cs,cs,cs
    qv[8] = cs
    qv[9] = cs
    qv[10] = cs
    qv[11] = 1.0
    // P3: cs,-cs,cs
    qv[12] = cs
    qv[13] = 0.0 - cs
    qv[14] = cs
    qv[15] = 1.0
    // P4: -cs,-cs,-cs
    qv[16] = 0.0 - cs
    qv[17] = 0.0 - cs
    qv[18] = 0.0 - cs
    qv[19] = 1.0
    // P5: -cs,cs,-cs
    qv[20] = 0.0 - cs
    qv[21] = cs
    qv[22] = 0.0 - cs
    qv[23] = 1.0
    // P6: cs,cs,-cs
    qv[24] = cs
    qv[25] = cs
    qv[26] = 0.0 - cs
    qv[27] = 1.0
    // P7: cs,-cs,-cs
    qv[28] = cs
    qv[29] = 0.0 - cs
    qv[30] = 0.0 - cs
    qv[31] = 1.0
    // P8: center (0,0,0)
    qv[32] = 0.0
    qv[33] = 0.0
    qv[34] = 0.0
    qv[35] = 1.0

    // Edge indices for normal calculation (6 faces, 3 vertex indices each)
    var edges = [0,1,2, 3,2,6, 7,6,5, 4,5,1, 4,0,3, 1,5,6]

    // Calculate normals for 6 faces
    var normals = fill(24, 0.0)
    var fi: int = 0
    while (fi < 6) {
        var e0 = edges[fi * 3]
        var e1 = edges[fi * 3 + 1]
        var e2 = edges[fi * 3 + 2]
        var n = calc_normal(
            [qv[e0 * 4], qv[e0 * 4 + 1], qv[e0 * 4 + 2]],
            [qv[e1 * 4], qv[e1 * 4 + 1], qv[e1 * 4 + 2]],
            [qv[e2 * 4], qv[e2 * 4 + 1], qv[e2 * 4 + 2]])
        normals[fi * 4] = n[0]
        normals[fi * 4 + 1] = n[1]
        normals[fi * 4 + 2] = n[2]
        normals[fi * 4 + 3] = n[3]
        fi = fi + 1
    }

    // MQube starts as identity
    var mqube = mat4_identity()
    var origin = [150.0, 150.0, 20.0, 1.0]

    // Initial translation to origin
    var mtrans = translate_mat(mat4_identity(), origin[0], origin[1], origin[2])
    mqube = mat4_mul(mtrans, mqube)

    // Transform all 9 points
    var pi: int = 0
    while (pi < 9) {
        var old_v = [qv[pi * 4], qv[pi * 4 + 1], qv[pi * 4 + 2], qv[pi * 4 + 3]]
        var new_v = vmulti(mtrans, old_v)
        qv[pi * 4] = new_v[0]
        qv[pi * 4 + 1] = new_v[1]
        qv[pi * 4 + 2] = new_v[2]
        qv[pi * 4 + 3] = new_v[3]
        pi = pi + 1
    }

    // Loop: rotate and draw, 51 iterations
    var loop_count: int = 0
    let loop_max = 50
    var last_px = 0
    while (loop_count <= loop_max) {
        // Build transform: translate to center, rotate, translate back
        var center_v = [qv[32], qv[33], qv[34], qv[35]]
        mtrans = translate_mat(mat4_identity(), 0.0 - center_v[0], 0.0 - center_v[1], 0.0 - center_v[2])
        mtrans = rotate_x(mtrans, 1.0)
        mtrans = rotate_y(mtrans, 3.0)
        mtrans = rotate_z(mtrans, 5.0)
        mtrans = translate_mat(mtrans, center_v[0], center_v[1], center_v[2])
        mqube = mat4_mul(mtrans, mqube)

        // Transform vertices
        pi = 8
        while (pi >= 0) {
            var old2 = [qv[pi * 4], qv[pi * 4 + 1], qv[pi * 4 + 2], qv[pi * 4 + 3]]
            var new2 = vmulti(mtrans, old2)
            qv[pi * 4] = new2[0]
            qv[pi * 4 + 1] = new2[1]
            qv[pi * 4 + 2] = new2[2]
            qv[pi * 4 + 3] = new2[3]
            pi = pi - 1
        }

        // Calculate current normals and draw visible faces
        var cur_n = fill(18, 0.0)
        var ni: int = 0
        while (ni < 6) {
            var nv = vmulti2(mqube, [normals[ni * 4], normals[ni * 4 + 1], normals[ni * 4 + 2]])
            cur_n[ni * 3] = nv[0]
            cur_n[ni * 3 + 1] = nv[1]
            cur_n[ni * 3 + 2] = nv[2]
            ni = ni + 1
        }

        // 12 line drawn flags
        var line_drawn = fill(12, 0)
        last_px = 0

        // Face 0: vertices 0,1,2,3 (lines 0,1,2,3)
        if (cur_n[0 * 3 + 2] < 0.0) {
            if (line_drawn[0] == 0) {
                last_px = draw_line([qv[0], qv[1]], [qv[4], qv[5]], last_px)
                line_drawn[0] = 1
            }
            if (line_drawn[1] == 0) {
                last_px = draw_line([qv[4], qv[5]], [qv[8], qv[9]], last_px)
                line_drawn[1] = 1
            }
            if (line_drawn[2] == 0) {
                last_px = draw_line([qv[8], qv[9]], [qv[12], qv[13]], last_px)
                line_drawn[2] = 1
            }
            if (line_drawn[3] == 0) {
                last_px = draw_line([qv[12], qv[13]], [qv[0], qv[1]], last_px)
                line_drawn[3] = 1
            }
        }

        // Face 1: vertices 3,2,6,7 (lines 2,9,6,10)
        if (cur_n[1 * 3 + 2] < 0.0) {
            if (line_drawn[2] == 0) {
                last_px = draw_line([qv[12], qv[13]], [qv[8], qv[9]], last_px)
                line_drawn[2] = 1
            }
            if (line_drawn[9] == 0) {
                last_px = draw_line([qv[8], qv[9]], [qv[24], qv[25]], last_px)
                line_drawn[9] = 1
            }
            if (line_drawn[6] == 0) {
                last_px = draw_line([qv[24], qv[25]], [qv[28], qv[29]], last_px)
                line_drawn[6] = 1
            }
            if (line_drawn[10] == 0) {
                last_px = draw_line([qv[28], qv[29]], [qv[12], qv[13]], last_px)
                line_drawn[10] = 1
            }
        }

        // Face 2: vertices 4,5,6,7 (lines 4,5,6,7)
        if (cur_n[2 * 3 + 2] < 0.0) {
            if (line_drawn[4] == 0) {
                last_px = draw_line([qv[16], qv[17]], [qv[20], qv[21]], last_px)
                line_drawn[4] = 1
            }
            if (line_drawn[5] == 0) {
                last_px = draw_line([qv[20], qv[21]], [qv[24], qv[25]], last_px)
                line_drawn[5] = 1
            }
            if (line_drawn[6] == 0) {
                last_px = draw_line([qv[24], qv[25]], [qv[28], qv[29]], last_px)
                line_drawn[6] = 1
            }
            if (line_drawn[7] == 0) {
                last_px = draw_line([qv[28], qv[29]], [qv[16], qv[17]], last_px)
                line_drawn[7] = 1
            }
        }

        // Face 3: vertices 4,5,1,0 (lines 4,8,0,11)
        if (cur_n[3 * 3 + 2] < 0.0) {
            if (line_drawn[4] == 0) {
                last_px = draw_line([qv[16], qv[17]], [qv[20], qv[21]], last_px)
                line_drawn[4] = 1
            }
            if (line_drawn[8] == 0) {
                last_px = draw_line([qv[20], qv[21]], [qv[4], qv[5]], last_px)
                line_drawn[8] = 1
            }
            if (line_drawn[0] == 0) {
                last_px = draw_line([qv[4], qv[5]], [qv[0], qv[1]], last_px)
                line_drawn[0] = 1
            }
            if (line_drawn[11] == 0) {
                last_px = draw_line([qv[0], qv[1]], [qv[16], qv[17]], last_px)
                line_drawn[11] = 1
            }
        }

        // Face 4: vertices 4,0,3,7 (lines 11,3,10,7)
        if (cur_n[4 * 3 + 2] < 0.0) {
            if (line_drawn[11] == 0) {
                last_px = draw_line([qv[16], qv[17]], [qv[0], qv[1]], last_px)
                line_drawn[11] = 1
            }
            if (line_drawn[3] == 0) {
                last_px = draw_line([qv[0], qv[1]], [qv[12], qv[13]], last_px)
                line_drawn[3] = 1
            }
            if (line_drawn[10] == 0) {
                last_px = draw_line([qv[12], qv[13]], [qv[28], qv[29]], last_px)
                line_drawn[10] = 1
            }
            if (line_drawn[7] == 0) {
                last_px = draw_line([qv[28], qv[29]], [qv[16], qv[17]], last_px)
                line_drawn[7] = 1
            }
        }

        // Face 5: vertices 1,5,6,2 (lines 8,5,9,1)
        if (cur_n[5 * 3 + 2] < 0.0) {
            if (line_drawn[8] == 0) {
                last_px = draw_line([qv[4], qv[5]], [qv[20], qv[21]], last_px)
                line_drawn[8] = 1
            }
            if (line_drawn[5] == 0) {
                last_px = draw_line([qv[20], qv[21]], [qv[24], qv[25]], last_px)
                line_drawn[5] = 1
            }
            if (line_drawn[9] == 0) {
                last_px = draw_line([qv[24], qv[25]], [qv[8], qv[9]], last_px)
                line_drawn[9] = 1
            }
            if (line_drawn[1] == 0) {
                last_px = draw_line([qv[8], qv[9]], [qv[4], qv[5]], last_px)
                line_drawn[1] = 1
            }
        }

        loop_count = loop_count + 1
    }

    // Verification: sum all vertex components
    var sum = 0.0
    pi = 0
    while (pi < 9) {
        sum = sum + qv[pi * 4] + qv[pi * 4 + 1] + qv[pi * 4 + 2] + qv[pi * 4 + 3]
        pi = pi + 1
    }
    return sum
}

pn run() {
    // Validate across 4 cube sizes: 20, 40, 80, 160
    // JS expected: all sizes produce sum ~= 2889.0
    var pass = true
    var sz: int = 20
    while (sz <= 160) {
        var sum = run_cube(sz)
        var expected = 2889.0
        var diff = sum - expected
        if (diff < 0.0) {
            diff = 0.0 - diff
        }
        if (diff > 0.001) {
            print("3d-cube: FAIL for size=" ++ string(sz) ++ " sum=" ++ string(sum) ++ "\n")
            pass = false
        }
        sz = sz * 2
    }
    return pass
}

pn main() {
    var __t0 = clock()
    // JetStream runs 8 iterations
    var pass = true
    var iter: int = 0
    while (iter < 8) {
        if (run() == false) {
            pass = false
        }
        iter = iter + 1
    }
    var __t1 = clock()
    if (pass) {
        print("3d-cube: PASS\n")
    } else {
        print("3d-cube: FAIL\n")
    }
    print("__TIMING__:" ++ string((__t1 - __t0) * 1000.0) ++ "\n")
}

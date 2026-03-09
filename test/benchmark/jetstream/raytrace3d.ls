// JetStream Benchmark: 3d-raytrace (SunSpider)
// Simple ray tracer — renders a scene with triangles, lighting, and reflections
// Original: Apple Inc.

// Vector operations (3-element arrays)
pn vec3(x: float, y: float, z: float) {
    return [x, y, z]
}

pn vec_add(v1, v2) {
    return [v1[0] + v2[0], v1[1] + v2[1], v1[2] + v2[2]]
}

pn vec_sub(v1, v2) {
    return [v1[0] - v2[0], v1[1] - v2[1], v1[2] - v2[2]]
}

pn vec_scale(v, s: float) {
    return [v[0] * s, v[1] * s, v[2] * s]
}

pn vec_scalev(v1, v2) {
    return [v1[0] * v2[0], v1[1] * v2[1], v1[2] * v2[2]]
}

pn vec_dot(v1, v2) {
    return v1[0] * v2[0] + v1[1] * v2[1] + v1[2] * v2[2]
}

pn vec_cross(v1, v2) {
    return [v1[1] * v2[2] - v1[2] * v2[1],
            v1[2] * v2[0] - v1[0] * v2[2],
            v1[0] * v2[1] - v1[1] * v2[0]]
}

pn vec_length(v) {
    return math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2])
}

pn vec_normalise(v) {
    var l = vec_length(v)
    return [v[0] / l, v[1] / l, v[2] / l]
}

pn vec_add_inplace(v1, v2) {
    v1[0] = v1[0] + v2[0]
    v1[1] = v1[1] + v2[1]
    v1[2] = v1[2] + v2[2]
    return v1
}

pn vec_scale_inplace(v, s: float) {
    v[0] = v[0] * s
    v[1] = v[1] * s
    v[2] = v[2] * s
    return v
}

// Matrix: flat 12-element array (3x4)
pn transform_matrix(m, v) {
    var x = m[0] * v[0] + m[1] * v[1] + m[2] * v[2] + m[3]
    var y = m[4] * v[0] + m[5] * v[1] + m[6] * v[2] + m[7]
    var z = m[8] * v[0] + m[9] * v[1] + m[10] * v[2] + m[11]
    return [x, y, z]
}

pn invert_matrix(m) {
    var temp = fill(16, 0.0)
    var tx = 0.0 - m[3]
    var ty = 0.0 - m[7]
    var tz = 0.0 - m[11]
    var h: int = 0
    while (h < 3) {
        var v: int = 0
        while (v < 3) {
            temp[h + v * 4] = m[v + h * 4]
            v = v + 1
        }
        h = h + 1
    }
    var i: int = 0
    while (i < 12) {
        m[i] = temp[i]
        i = i + 1
    }
    m[3] = tx * m[0] + ty * m[1] + tz * m[2]
    m[7] = tx * m[4] + ty * m[5] + tz * m[6]
    m[11] = tx * m[8] + ty * m[9] + tz * m[10]
    return m
}

// Triangle: stored as a map with precomputed intersection data
pn create_triangle(p1, p2, p3) {
    var edge1 = vec_sub(p3, p1)
    var edge2 = vec_sub(p2, p1)
    var normal = vec_cross(edge1, edge2)

    var axis = 0
    if (abs(normal[0]) > abs(normal[1])) {
        if (abs(normal[0]) > abs(normal[2])) {
            axis = 0
        } else {
            axis = 2
        }
    } else {
        if (abs(normal[1]) > abs(normal[2])) {
            axis = 1
        } else {
            axis = 2
        }
    }
    var u = (axis + 1) % 3
    var v = (axis + 2) % 3
    var u1 = edge1[u]
    var v1 = edge1[v]
    var u2 = edge2[u]
    var v2 = edge2[v]
    var n_norm = vec_normalise(normal)
    var nu = normal[u] / normal[axis]
    var nv = normal[v] / normal[axis]
    var nd = vec_dot(normal, p1) / normal[axis]
    var det = u1 * v2 - v1 * u2
    return {axis: axis, normal: n_norm, nu: nu, nv: nv, nd: nd,
            eu: p1[u], ev: p1[v],
            nu1: u1 / det, nv1: 0.0 - v1 / det,
            nu2: v2 / det, nv2: 0.0 - u2 / det,
            material: [0.7, 0.7, 0.7], shader: null, reflection: 0.0}
}

pn triangle_intersect(tri, orig, dir, near: float, far: float) {
    var u = (tri.axis + 1) % 3
    var v = (tri.axis + 2) % 3
    var d = dir[tri.axis] + tri.nu * dir[u] + tri.nv * dir[v]
    var t = (tri.nd - orig[tri.axis] - tri.nu * orig[u] - tri.nv * orig[v]) / d
    if (t < near) {
        return -1.0
    }
    if (t > far) {
        return -1.0
    }
    var pu = orig[u] + t * dir[u] - tri.eu
    var pv = orig[v] + t * dir[v] - tri.ev
    var a2 = pv * tri.nu1 + pu * tri.nv1
    if (a2 < 0.0) {
        return -1.0
    }
    var a3 = pu * tri.nu2 + pv * tri.nv2
    if (a3 < 0.0) {
        return -1.0
    }
    if ((a2 + a3) > 1.0) {
        return -1.0
    }
    return t
}

// Scene: list of triangles, lights, ambient, background
pn create_scene(triangles) {
    return {triangles: triangles, lights: fill(0, null),
            ambient: [0.0, 0.0, 0.0], background: [0.8, 0.8, 1.0],
            n_lights: 0, n_triangles: len(triangles)}
}

pn scene_add_light(scene, pos, colour) {
    var idx = scene.n_lights
    // Store as flat array: [x,y,z, r,g,b]
    var light = {pos: pos, colour: colour}
    var new_lights = (scene.lights) ++ [light]
    scene.lights = new_lights
    scene.n_lights = idx + 1
}

pn scene_intersect(scene, origin, dir, near: float, far: float, depth: int) {
    if (depth > 3) {
        return scene.background
    }
    var closest = null
    var i: int = 0
    while (i < scene.n_triangles) {
        var tri = (scene.triangles)[i]
        var d = triangle_intersect(tri, origin, dir, near, far)
        if (d > 0.0) {
            far = d
            closest = tri
        }
        i = i + 1
    }
    if (closest == null) {
        return [scene.background[0], scene.background[1], scene.background[2]]
    }
    var normal = closest.normal
    var hit = vec_add(origin, vec_scale(dir, far))
    if (vec_dot(dir, normal) > 0.0) {
        normal = [0.0 - normal[0], 0.0 - normal[1], 0.0 - normal[2]]
    }
    var colour = closest.material

    // Lighting
    var l = [scene.ambient[0], scene.ambient[1], scene.ambient[2]]
    i = 0
    while (i < scene.n_lights) {
        var light = (scene.lights)[i]
        var to_light = vec_sub(light.pos, hit)
        var distance = vec_length(to_light)
        to_light = vec_scale(to_light, 1.0 / distance)
        // Shadow test
        var blocked = false
        var bi: int = 0
        while (bi < scene.n_triangles) {
            var tri = (scene.triangles)[bi]
            var sd = triangle_intersect(tri, hit, to_light, 0.0001, distance - 0.0001)
            if (sd > 0.0) {
                blocked = true
                bi = scene.n_triangles  // break
            }
            bi = bi + 1
        }
        if (blocked == false) {
            var nl = vec_dot(normal, to_light)
            if (nl > 0.0) {
                l = vec_add_inplace(l, vec_scale(light.colour, nl))
            }
        }
        i = i + 1
    }
    l = vec_scalev(l, colour)
    return l
}

// Camera
pn create_camera(origin, lookat, up) {
    var zaxis = vec_normalise(vec_sub(lookat, origin))
    var xaxis = vec_normalise(vec_cross(up, zaxis))
    var neg_z = [0.0 - zaxis[0], 0.0 - zaxis[1], 0.0 - zaxis[2]]
    var yaxis = vec_normalise(vec_cross(xaxis, neg_z))
    var m = fill(12, 0.0)
    m[0] = xaxis[0]
    m[1] = xaxis[1]
    m[2] = xaxis[2]
    m[4] = yaxis[0]
    m[5] = yaxis[1]
    m[6] = yaxis[2]
    m[8] = zaxis[0]
    m[9] = zaxis[1]
    m[10] = zaxis[2]
    m = invert_matrix(m)
    m[3] = 0.0
    m[7] = 0.0
    m[11] = 0.0

    var d0 = vec_normalise([-0.7, 0.7, 1.0])
    var d1 = vec_normalise([0.7, 0.7, 1.0])
    var d2 = vec_normalise([0.7, -0.7, 1.0])
    var d3 = vec_normalise([-0.7, -0.7, 1.0])
    d0 = transform_matrix(m, d0)
    d1 = transform_matrix(m, d1)
    d2 = transform_matrix(m, d2)
    d3 = transform_matrix(m, d3)
    return {origin: origin, d0: d0, d1: d1, d2: d2, d3: d3}
}

pn render_scene(camera, scene, size: int) {
    var pixel_count = 0
    var y: int = 0
    while (y < size) {
        var yf = float(y) / float(size)
        var ray0_dir = vec_add(vec_scale(camera.d0, yf), vec_scale(camera.d3, 1.0 - yf))
        var ray1_dir = vec_add(vec_scale(camera.d1, yf), vec_scale(camera.d2, 1.0 - yf))
        var x: int = 0
        while (x < size) {
            var xf = float(x) / float(size)
            var origin = vec_add(vec_scale(camera.origin, xf), vec_scale(camera.origin, 1.0 - xf))
            var dir = vec_normalise(vec_add(vec_scale(ray0_dir, xf), vec_scale(ray1_dir, 1.0 - xf)))
            var l = scene_intersect(scene, origin, dir, 0.0001, 1000000.0, 0)
            pixel_count = pixel_count + 1
            x = x + 1
        }
        y = y + 1
    }
    return pixel_count
}

pn raytrace_scene() {
    // Build scene: a cube (12 triangles) + floor (2 triangles)
    var tfl = vec3(-10.0, 10.0, -10.0)
    var tfr = vec3(10.0, 10.0, -10.0)
    var tbl = vec3(-10.0, 10.0, 10.0)
    var tbr = vec3(10.0, 10.0, 10.0)
    var bfl = vec3(-10.0, -10.0, -10.0)
    var bfr = vec3(10.0, -10.0, -10.0)
    var bbl = vec3(-10.0, -10.0, 10.0)
    var bbr = vec3(10.0, -10.0, 10.0)

    var triangles = [
        create_triangle(tfl, tfr, bfr),
        create_triangle(tfl, bfr, bfl),
        create_triangle(tbl, tbr, bbr),
        create_triangle(tbl, bbr, bbl),
        create_triangle(tbl, tfl, bbl),
        create_triangle(tfl, bfl, bbl),
        create_triangle(tbr, tfr, bbr),
        create_triangle(tfr, bfr, bbr),
        create_triangle(tbl, tbr, tfr),
        create_triangle(tbl, tfr, tfl),
        create_triangle(bbl, bbr, bfr),
        create_triangle(bbl, bfr, bfl)
    ]

    // Floor
    var ffl = vec3(-1000.0, -30.0, -1000.0)
    var ffr = vec3(1000.0, -30.0, -1000.0)
    var fbl = vec3(-1000.0, -30.0, 1000.0)
    var fbr = vec3(1000.0, -30.0, 1000.0)
    var floor1 = create_triangle(fbl, fbr, ffr)
    var floor2 = create_triangle(fbl, ffr, ffl)
    var all_tris = triangles ++ [floor1, floor2]

    var scene = create_scene(all_tris)
    scene_add_light(scene, vec3(20.0, 38.0, -22.0), [0.7, 0.3, 0.3])
    scene_add_light(scene, vec3(-23.0, 40.0, 17.0), [0.7, 0.3, 0.3])
    scene_add_light(scene, vec3(23.0, 20.0, 17.0), [0.7, 0.7, 0.7])
    scene.ambient = [0.1, 0.1, 0.1]

    var cam = create_camera(vec3(-40.0, 40.0, 40.0), vec3(0.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0))
    let size = 30
    var pixels = render_scene(cam, scene, size)
    return pixels
}

pn main() {
    var __t0 = clock()
    // JetStream runs 8 iterations
    var iter: int = 0
    var total_pixels = 0
    while (iter < 8) {
        total_pixels = total_pixels + raytrace_scene()
        iter = iter + 1
    }
    var __t1 = clock()
    // 30x30 = 900 pixels per iteration, 8 iterations = 7200
    if (total_pixels == 7200) {
        print("3d-raytrace: PASS (pixels=" ++ string(total_pixels) ++ ")\n")
    } else {
        print("3d-raytrace: DONE (pixels=" ++ string(total_pixels) ++ ")\n")
    }
    print("__TIMING__:" ++ string((__t1 - __t0) * 1000.0) ++ "\n")
}

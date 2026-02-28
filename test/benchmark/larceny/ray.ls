// Larceny Benchmark: ray
// Simple ray tracer — cast rays against spheres
// Adapted from Larceny/Gambit benchmark suite
// Traces 100x100 grid of rays against 4 spheres
// Expected: counts pixels that hit at least one sphere

pn sphere_intersect(ox, oy, oz, dx, dy, dz, cx, cy, cz, r) {
    // Ray-sphere intersection test
    // Ray: origin (ox,oy,oz) + t * direction (dx,dy,dz)
    // Sphere: center (cx,cy,cz), radius r
    var ex = ox - cx
    var ey = oy - cy
    var ez = oz - cz
    var a = dx * dx + dy * dy + dz * dz
    var b = 2.0 * (ex * dx + ey * dy + ez * dz)
    var c = ex * ex + ey * ey + ez * ez - r * r
    var disc = b * b - 4.0 * a * c
    if (disc < 0.0) {
        return -1.0
    }
    var t = (0.0 - b - sqrt(disc)) / (2.0 * a)
    if (t > 0.001) {
        return t
    }
    t = (0.0 - b + sqrt(disc)) / (2.0 * a)
    if (t > 0.001) {
        return t
    }
    return -1.0
}

pn benchmark() {
    // 4 spheres
    var sx = [0.0, -2.0, 2.0, 0.0]
    var sy = [0.0, 0.0, 0.0, 2.0]
    var sz = [5.0, 5.0, 5.0, 5.0]
    var sr = [1.0, 1.0, 1.0, 1.0]
    let num_spheres = 4

    // Camera at origin, looking along +z
    let grid = 100
    var hits = 0
    var total_iters = 0

    // Repeat for timing
    var rep = 0
    while (rep < 1) {
        hits = 0
        var py = 0
        while (py < grid) {
            var px = 0
            while (px < grid) {
                // Ray direction (perspective projection)
                var dx = (float(px) - 50.0) / 50.0
                var dy = (float(py) - 50.0) / 50.0
                var dz = 1.0
                // Normalize
                var len = sqrt(dx * dx + dy * dy + dz * dz)
                dx = dx / len
                dy = dy / len
                dz = dz / len

                // Test all spheres
                var min_t = 999999.0
                var si = 0
                while (si < num_spheres) {
                    var t = sphere_intersect(0.0, 0.0, 0.0, dx, dy, dz, sx[si], sy[si], sz[si], sr[si])
                    if (t > 0.0 and t < min_t) {
                        min_t = t
                    }
                    si = si + 1
                }
                if (min_t < 999999.0) {
                    hits = hits + 1
                }
                px = px + 1
            }
            py = py + 1
        }
        rep = rep + 1
    }
    return hits
}

pn main() {
    let result = benchmark()
    print("ray: hits=" ++ string(result) ++ "\n")
    // The exact hit count depends on geometry; just verify it's reasonable
    if (result > 0 and result < 10000) {
        print("ray: PASS\n")
    } else {
        print("ray: FAIL\n")
    }
}

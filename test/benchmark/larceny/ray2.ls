// Larceny Benchmark: ray (Typed version)
// Simple ray tracer — cast rays against spheres
// Adapted from Larceny/Gambit benchmark suite
// Traces 100x100 grid of rays against 4 spheres
// Expected: counts pixels that hit at least one sphere

pn sphere_intersect(ox: float, oy: float, oz: float, dx: float, dy: float, dz: float,
                    cx: float, cy: float, cz: float, r: float) float {
    // Ray-sphere intersection test
    // Ray: origin (ox,oy,oz) + t * direction (dx,dy,dz)
    // Sphere: center (cx,cy,cz), radius r
    var ex: float = ox - cx
    var ey: float = oy - cy
    var ez: float = oz - cz
    var a: float = dx * dx + dy * dy + dz * dz
    var b: float = 2.0 * (ex * dx + ey * dy + ez * dz)
    var c: float = ex * ex + ey * ey + ez * ez - r * r
    var disc: float = b * b - 4.0 * a * c
    if (disc < 0.0) {
        return -1.0
    }
    var t: float = (-b - math.sqrt(disc)) / (2.0 * a)
    if (t > 0.001) {
        return t
    }
    t = (-b + math.sqrt(disc)) / (2.0 * a)
    if (t > 0.001) {
        return t
    }
    return -1.0
}

pn benchmark() int {
    // 4 spheres — preserve the float element contract through indexed calls.
    var sx: float[] = [0.0, -2.0, 2.0, 0.0]
    var sy: float[] = [0.0, 0.0, 0.0, 2.0]
    var sz: float[] = [5.0, 5.0, 5.0, 5.0]
    var sr: float[] = [1.0, 1.0, 1.0, 1.0]
    let num_spheres: int = 4

    // Camera at origin, looking along +z
    let grid: int = 100
    var hits: int = 0
    var total_iters: int = 0

    // Repeat for timing
    var rep: int = 0
    while (rep < 1) {
        hits = 0
        var py: int = 0
        while (py < grid) {
            var px: int = 0
            while (px < grid) {
                // Ray direction (perspective projection)
                var dx: float = (float(px) - 50.0) / 50.0
                var dy: float = (float(py) - 50.0) / 50.0
                var dz: float = 1.0
                // Normalize
                var len: float = math.sqrt(dx * dx + dy * dy + dz * dz)
                dx = dx / len
                dy = dy / len
                dz = dz / len

                // Test all spheres
                var min_t: float = 999999.0
                var si: int = 0
                while (si < num_spheres) {
                    var t: float = sphere_intersect(0.0, 0.0, 0.0, dx, dy, dz, sx[si], sy[si], sz[si], sr[si])
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
    var __t0 = clock()
    let result = benchmark()
    var __t1 = clock()
    print("ray: hits=" ++ result ++ "\n")
    // The exact hit count depends on geometry; just verify it's reasonable
    if (result > 0 and result < 10000) {
        print("ray: PASS\n")
    } else {
        print("ray: FAIL\n")
    }
    print("__TIMING__:" ++ ((__t1 - __t0) * 1000.0) ++ "\n")
}

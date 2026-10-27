// Generated geometry as plain buffers, for geometry elements and for test oracles that
// must draw exactly the same vertices.

// A unit-diameter sphere: `rings` bands from pole to pole, `segments` around.
// u runs around with the angle, v up from the south pole; the tangent points
// along +u and the bitangent (normal x tangent) along +v.
pub fn sphere(rings, segments, tangents = false) {
    let points = [for (i in 0 to rings, j in 0 to segments,
        let phi = math.pi * i / rings, let theta = 2.0 * math.pi * j / segments)
        {p: [math.sin(phi) * math.cos(theta), math.cos(phi), 0.0 - math.sin(phi) * math.sin(theta)],
         uv: [j / float(segments), 1.0 - i / float(rings)], t: [0.0 - math.sin(theta), 0.0, 0.0 - math.cos(theta), 1.0]}]
    let row = segments + 1;
    {positions: [for (q in points, x in q.p) x * 0.5], normals: [for (q in points, x in q.p) x], uvs: [for (q in points, x in q.uv) x],
     tangents: if (tangents) [for (q in points, x in q.t) x] else null,
     indices: [for (i in 0 to rings - 1, j in 0 to segments - 1, let a = i * row + j, let b = a + row)
        *[a, b, a + 1, b, b + 1, a + 1]]}
}

// Generated geometry as plain buffers, for geometry elements and for test oracles that
// must draw exactly the same vertices.

// A unit-diameter sphere: `rings` bands from pole to pole, `segments` around.
// u runs around with the angle, v up from the south pole; the tangent points
// along +u and the bitangent (normal x tangent) along +v.
pub fn sphere(rings, segments, tangents = false) {
    // Each longitude is shared by every latitude. Build the flat channels
    // directly and generate tangent data only when the caller needs it.
    let meridians = [for (j in 0 to segments, let theta = 2.0 * math.pi * j / segments)
        [math.cos(theta), math.sin(theta), j / float(segments)]]
    let normals = [for (i in 0 to rings, let phi = math.pi * i / rings,
        let radius = math.sin(phi), let y = math.cos(phi)) for (q in meridians)
        *[radius * q[0], y, 0.0 - radius * q[1]]]
    let row = segments + 1;
    {positions: normals * 0.5, normals: normals,
     uvs: [for (i in 0 to rings, let v = 1.0 - i / float(rings)) for (q in meridians) *[q[2], v]],
     tangents: if (tangents) [for (i in 0 to rings, q in meridians) *[0.0 - q[1], 0.0, 0.0 - q[0], 1.0]] else null,
     indices: [for (i in 0 to rings - 1, j in 0 to segments - 1, let a = i * row + j, let b = a + row)
        *[a, b, a + 1, b, b + 1, a + 1]]}
}

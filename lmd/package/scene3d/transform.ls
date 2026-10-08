fn numeric(v) bool => v is number and abs(v) < inf
pub fn vector(v, size) bool => v is array and len(v) == size and all([for (x in v) numeric(x)])

pub fn matrix(position, rotation, scale) array^ {
    let checked = if (not vector(position, 3) or not vector(rotation, 3) or not vector(scale, 3) or any([for (x in scale) x == 0]))
        raise error("scene3d: invalid transform")
    let cx = cos(rotation[0]), sx = sin(rotation[0])
    let cy = cos(rotation[1]), sy = sin(rotation[1])
    let cz = cos(rotation[2]), sz = sin(rotation[2]);
    [cy * cz * scale[0], (sx * sy * cz + cx * sz) * scale[0], (-cx * sy * cz + sx * sz) * scale[0], 0.0,
     -cy * sz * scale[1], (-sx * sy * sz + cx * cz) * scale[1], (cx * sy * sz + sx * cz) * scale[1], 0.0,
     sy * scale[2], -sx * cy * scale[2], cx * cy * scale[2], 0.0,
     position[0], position[1], position[2], 1.0]
}
pub fn multiply(a, b) array^ {
    let checked = if (not vector(a, 16) or not vector(b, 16)) raise error("scene3d: expected 4x4 matrices");
    [for (c in 0 to 3) for (r in 0 to 3) sum([for (k in 0 to 3) a[k * 4 + r] * b[c * 4 + k]])]
}
pub fn point(m, p) array^ {
    let checked = if (not vector(m, 16) or not vector(p, 3)) raise error("scene3d: invalid matrix or point");
    [for (r in 0 to 2) m[r] * p[0] + m[4 + r] * p[1] + m[8 + r] * p[2] + m[12 + r]]
}

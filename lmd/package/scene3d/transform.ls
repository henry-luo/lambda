pub fn numeric(v) bool => v is number and abs(v) <= 3.4028234e38
pub fn vector(v, size) bool => v is array and len(v) == size and all([for (x in v) numeric(x)])

pub fn affine(m) bool => vector(m, 16) and m[3] == 0 and m[7] == 0 and m[11] == 0 and m[15] == 1 and
    abs(m[0] * (m[5] * m[10] - m[9] * m[6]) - m[4] * (m[1] * m[10] - m[9] * m[2]) +
        m[8] * (m[1] * m[6] - m[5] * m[2])) >= 0.0000000001

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
// the inverse of an affine matrix: the 3x3 adjugate over its determinant, then the translation
pub fn inverse(m) array^ {
    let checked = if (not affine(m)) raise error("scene3d: expected an invertible affine matrix")
    let d = m[0] * (m[5] * m[10] - m[9] * m[6]) - m[4] * (m[1] * m[10] - m[9] * m[2]) + m[8] * (m[1] * m[6] - m[5] * m[2])
    let r = [(m[5] * m[10] - m[6] * m[9]) / d, (m[9] * m[2] - m[1] * m[10]) / d, (m[1] * m[6] - m[5] * m[2]) / d,
             (m[8] * m[6] - m[4] * m[10]) / d, (m[0] * m[10] - m[8] * m[2]) / d, (m[4] * m[2] - m[0] * m[6]) / d,
             (m[4] * m[9] - m[8] * m[5]) / d, (m[8] * m[1] - m[0] * m[9]) / d, (m[0] * m[5] - m[4] * m[1]) / d];
    [r[0], r[1], r[2], 0.0, r[3], r[4], r[5], 0.0, r[6], r[7], r[8], 0.0,
     0.0 - (r[0] * m[12] + r[3] * m[13] + r[6] * m[14]),
     0.0 - (r[1] * m[12] + r[4] * m[13] + r[7] * m[14]),
     0.0 - (r[2] * m[12] + r[5] * m[13] + r[8] * m[14]), 1.0]
}
pub fn point(m, p) array^ {
    let checked = if (not vector(m, 16) or not vector(p, 3)) raise error("scene3d: invalid matrix or point");
    [for (r in 0 to 2) m[r] * p[0] + m[4 + r] * p[1] + m[8 + r] * p[2] + m[12 + r]]
}

// [x, y, z, w] quaternions; the motion package's poses and retargeting use them
pub fn quaternion_multiply(a, b) => [
    a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1],
    a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
    a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3],
    a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]]
// the inverse of a unit quaternion is its conjugate
pub fn quaternion_inverse(q) => [0.0 - q[0], 0.0 - q[1], 0.0 - q[2], q[3]]
pub fn quaternion_normalize(q) {
    let length = sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (length > 0) [q[0] / length, q[1] / length, q[2] / length, q[3] / length] else [0.0, 0.0, 0.0, 1.0]
}
pub fn quaternion_rotate(q, v) {
    let p = quaternion_multiply(quaternion_multiply(q, [v[0], v[1], v[2], 0.0]), quaternion_inverse(q));
    [p[0], p[1], p[2]]
}
// a column-major affine matrix from a translation, a unit quaternion and a scale
pub fn compose(position, q, scale) {
    let x = q[0], y = q[1], z = q[2], w = q[3];
    [(1 - 2 * (y * y + z * z)) * scale[0], 2 * (x * y + w * z) * scale[0], 2 * (x * z - w * y) * scale[0], 0.0,
     2 * (x * y - w * z) * scale[1], (1 - 2 * (x * x + z * z)) * scale[1], 2 * (y * z + w * x) * scale[1], 0.0,
     2 * (x * z + w * y) * scale[2], 2 * (y * z - w * x) * scale[2], (1 - 2 * (x * x + y * y)) * scale[2], 0.0,
     position[0], position[1], position[2], 1.0]
}

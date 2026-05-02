// pdf/util.ls — Shared utilities for the Lambda PDF package
//
// Math, formatting, and small generic helpers. Keep this file dependency-free
// so other modules can import it without cycles.

// ============================================================
// Numeric formatting
// ============================================================

// Format a number for SVG output: drops trailing zeros, max 4 decimals.
// Values within ~1e-4 of an integer are rendered as the integer.
pub fn fmt_num(n) {
    let f = float(n);
    if f == 0.0 { "0" }
    else {
        let r = round(f * 10000.0) / 10000.0;
        if r == float(int(r)) { string(int(r)) }
        else { string(r) }
    }
}

// Format a 6-element matrix as an SVG transform string: "matrix(a b c d e f)"
pub fn fmt_matrix(m) {
    "matrix(" ++ fmt_num(m[0]) ++ " " ++ fmt_num(m[1]) ++ " " ++
                 fmt_num(m[2]) ++ " " ++ fmt_num(m[3]) ++ " " ++
                 fmt_num(m[4]) ++ " " ++ fmt_num(m[5]) ++ ")"
}

// Format an RGB triple (0..1 floats) as "rgb(r,g,b)" with 0..255 ints.
pub fn fmt_rgb(r, g, b) {
    let ri = int(round(float(r) * 255.0));
    let gi = int(round(float(g) * 255.0));
    let bi = int(round(float(b) * 255.0));
    "rgb(" ++ string(ri) ++ "," ++ string(gi) ++ "," ++ string(bi) ++ ")"
}

// ============================================================
// Identity / default matrices
// ============================================================

pub IDENTITY = [1.0, 0.0, 0.0, 1.0, 0.0, 0.0]

// Compose two PDF affine matrices (6-element form):
//   [a b c d e f]  ==  [[a b 0][c d 0][e f 1]]
// Standard PDF convention: result = m1 * m2.
pub fn matrix_mul(m1, m2) {
    [
        m1[0] * m2[0] + m1[1] * m2[2],
        m1[0] * m2[1] + m1[1] * m2[3],
        m1[2] * m2[0] + m1[3] * m2[2],
        m1[2] * m2[1] + m1[3] * m2[3],
        m1[4] * m2[0] + m1[5] * m2[2] + m2[4],
        m1[4] * m2[1] + m1[5] * m2[3] + m2[5]
    ]
}

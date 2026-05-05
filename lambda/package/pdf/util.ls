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

// True when a 6-element matrix equals the identity within 1e-6 tolerance.
pub fn is_identity(m) {
    ((m[0] == 1.0) and (m[1] == 0.0) and (m[2] == 0.0)
        and (m[3] == 1.0) and (m[4] == 0.0) and (m[5] == 0.0))
}

// Absolute value of a numeric (int/float).
pub fn fabs(n) {
    let f = float(n);
    if (f < 0.0) { 0.0 - f } else { f }
}

// Coerce a PDF numeric operand to float, defaulting malformed/null input to 0.
pub fn num(v) {
    if (v == null)        { 0.0 }
    else if (v is float)  { v }
    else if (v is int)    { float(v) }
    else                  { 0.0 }
}

// Coerce a PDF numeric operand to int, preserving caller-provided fallback.
pub fn int_or(v, fallback) {
    if (v is int) { v }
    else if (v is float) { int(v) }
    else { fallback }
}

// Return a byte from a Lambda string, or 0 for missing/out-of-range bytes.
pub fn byte_at(s, i) {
    if (s == null or i < 0 or i >= len(s)) { 0 }
    else { ord(s[i]) }
}

// Extract a PDF name operand value. Content parser name operands are maps,
// while resolved dictionaries sometimes carry raw strings.
pub fn name_of(v) {
    if (v is map and v.kind == "name") { v.value }
    else if (v is string) { v }
    else { null }
}

pub fn hex_digit_value(c: string) {
    let k = ord(c)
    if ((k >= 48) and (k <= 57)) { k - 48 }
    else if ((k >= 65) and (k <= 70)) { k - 55 }
    else if ((k >= 97) and (k <= 102)) { k - 87 }
    else { 0 }
}

pub fn is_hex_digit(c: string) {
    let k = ord(c)
    ((k >= 48) and (k <= 57)) or ((k >= 65) and (k <= 70)) or ((k >= 97) and (k <= 102))
}

pub fn clean_hex(hex: string) {
    if (len(hex) == 0) { "" }
    else {
        let parts = for (i in 0 to (len(hex) - 1) where is_hex_digit(hex[i])) hex[i]
        parts | join("")
    }
}

pub fn hex_code_at(hex: string, i: int, digits: int) {
    if (digits == 8) {
        (hex_digit_value(hex[i]) * 268435456) + (hex_digit_value(hex[i + 1]) * 16777216) +
        (hex_digit_value(hex[i + 2]) * 1048576) + (hex_digit_value(hex[i + 3]) * 65536) +
        (hex_digit_value(hex[i + 4]) * 4096) + (hex_digit_value(hex[i + 5]) * 256) +
        (hex_digit_value(hex[i + 6]) * 16) + hex_digit_value(hex[i + 7])
    }
    else if (digits == 6) {
        (hex_digit_value(hex[i]) * 1048576) + (hex_digit_value(hex[i + 1]) * 65536) +
        (hex_digit_value(hex[i + 2]) * 4096) + (hex_digit_value(hex[i + 3]) * 256) +
        (hex_digit_value(hex[i + 4]) * 16) + hex_digit_value(hex[i + 5])
    }
    else if (digits == 4) {
        (hex_digit_value(hex[i]) * 4096) + (hex_digit_value(hex[i + 1]) * 256) +
        (hex_digit_value(hex[i + 2]) * 16) + hex_digit_value(hex[i + 3])
    }
    else {
        (hex_digit_value(hex[i]) * 16) + hex_digit_value(hex[i + 1])
    }
}

pub fn hex_byte_at(hex: string, i: int) {
    if (i + 1 < len(hex)) { hex_code_at(hex, i, 2) }
    else { hex_digit_value(hex[i]) * 16 }
}

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

// pdf/color.ls — PDF color-space helpers
//
// Phase 3 scope: handle the device color spaces every PDF uses unconditionally.
//   - DeviceGray (g, G)        — single value 0..1
//   - DeviceRGB  (rg, RG)      — three values 0..1
//   - DeviceCMYK (k, K)        — four values 0..1, naive conversion to sRGB
//
// Higher-level color spaces (cs/CS/sc/SC/SCN/scn, ICCBased, Indexed,
// Pattern, Separation, DeviceN) are deferred to a later phase.
//
// Every function here is `fn` and returns a fresh string ("rgb(r,g,b)").
// Black is the PDF default for both fill and stroke.

import util: .util

pub BLACK = "rgb(0,0,0)"

// ============================================================
// Operand helpers (mirror text._num)
// ============================================================

fn _num(op) {
    if (op == null)        { 0.0 }
    else if (op is float)  { op }
    else if (op is int)    { float(op) }
    else                   { 0.0 }
}

// Clamp a 0..1 component (PDFs occasionally produce slight overshoot).
fn _clamp01(v) {
    if (v < 0.0) { 0.0 }
    else if (v > 1.0) { 1.0 }
    else { v }
}

// ============================================================
// Public converters
// ============================================================

pub fn gray(v) {
    let g = _clamp01(_num(v))
    util.fmt_rgb(g, g, g)
}

pub fn rgb(r, g, b) {
    util.fmt_rgb(_clamp01(_num(r)), _clamp01(_num(g)), _clamp01(_num(b)))
}

// Naive CMYK→sRGB used by every reasonable PDF viewer when no ICC profile
// is present:  R = (1 - C)(1 - K)  etc.  Good enough for non-press output.
pub fn cmyk(c, m, y, k) {
    let cc = _clamp01(_num(c))
    let mm = _clamp01(_num(m))
    let yy = _clamp01(_num(y))
    let kk = _clamp01(_num(k))
    let rr = (1.0 - cc) * (1.0 - kk)
    let gg = (1.0 - mm) * (1.0 - kk)
    let bb = (1.0 - yy) * (1.0 - kk)
    util.fmt_rgb(rr, gg, bb)
}

// ============================================================
// Operand-list dispatchers (called from interp.ls)
//
// Each takes the operand list straight off the operator record and
// returns a CSS color string, falling back to BLACK if the operand
// shape is unexpected.
// ============================================================

pub fn from_g_ops(ops) {
    if (len(ops) >= 1) { gray(ops[0]) } else { BLACK }
}

pub fn from_rg_ops(ops) {
    if (len(ops) >= 3) { rgb(ops[0], ops[1], ops[2]) } else { BLACK }
}

pub fn from_k_ops(ops) {
    if (len(ops) >= 4) { cmyk(ops[0], ops[1], ops[2], ops[3]) } else { BLACK }
}

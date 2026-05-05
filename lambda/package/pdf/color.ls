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

import util:    .util
import resolve: .resolve

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

// sc/SC/scn/SCN: "set color in current colorspace". The PDF could have
// any colorspace active (DeviceGray/RGB/CMYK, ICCBased, Indexed,
// Pattern, Separation, DeviceN). Without resolving the actual cs
// dictionary, fall back to dispatching by operand count — matches what
// every real-world PDF uses for the device-family colorspaces.
//   1 numeric operand → DeviceGray
//   3 numeric operands → DeviceRGB
//   4 numeric operands → DeviceCMYK
// scn/SCN may carry a trailing /PatternName operand; the leading
// numeric operands still indicate the underlying tint values.
pub fn from_sc_ops(ops) {
    let nums = (for (op in ops where (op is float or op is int)) op)
    let n = len(nums)
    if      (n >= 4) { cmyk(nums[0], nums[1], nums[2], nums[3]) }
    else if (n >= 3) { rgb(nums[0], nums[1], nums[2]) }
    else if (n >= 1) { gray(nums[0]) }
    else             { BLACK }
}

// ============================================================
// Color-space aware conversion for cs/CS + sc/SC/scn/SCN
// ============================================================

fn _name_of(v) {
    if (v is map and v.kind == "name") { v.value }
    else if (v is string) { v }
    else { null }
}

fn _numeric_ops(ops) {
    for (op in ops where (op is float or op is int)) op
}

fn _resource_color_space(pdf, page, name) {
    let res = resolve.page_resources(pdf, page)
    let table = if (res and res.ColorSpace) resolve.deref(pdf, res.ColorSpace) else null
    if (table == null) { null }
    else { resolve.deref(pdf, table[name]) }
}

fn _resolve_space(pdf, page, cs) {
    let nm = _name_of(cs)
    if (nm == null) { cs }
    else if (nm == "DeviceRGB" or nm == "RGB" or
             nm == "DeviceGray" or nm == "G" or
             nm == "DeviceCMYK" or nm == "CMYK") { nm }
    else {
        let found = _resource_color_space(pdf, page, nm)
        if (found == null) { nm } else { found }
    }
}

fn _space_type(cs) {
    if (cs is array and len(cs) >= 1) { _name_of(cs[0]) }
    else { _name_of(cs) }
}

fn _base_component_count(base) {
    let b = _space_type(base)
    if (b == "DeviceGray" or b == "G" or b == "CalGray") { 1 }
    else if (b == "DeviceCMYK" or b == "CMYK") { 4 }
    else { 3 }
}

fn _byte_at(s, i) {
    if (s == null or i < 0 or i >= len(s)) { 0 }
    else { ord(s[i]) }
}

fn _indexed_color(cs, idx_raw) {
    if (not (cs is array) or len(cs) < 4) { BLACK }
    else {
        let base = cs[1]
        let hival = if (cs[2] is int) cs[2] else if (cs[2] is float) int(cs[2]) else 0
        let lookup = cs[3]
        let ncomp = _base_component_count(base)
        let idx0 = if (idx_raw is int) idx_raw else if (idx_raw is float) int(idx_raw) else 0
        let idx = if (idx0 < 0) 0 else if (idx0 > hival) hival else idx0
        let off = idx * ncomp
        let t = _space_type(base)
        if (t == "DeviceGray" or t == "G" or t == "CalGray") {
            gray(float(_byte_at(lookup, off)) / 255.0)
        }
        else if (t == "DeviceCMYK" or t == "CMYK") {
            cmyk(float(_byte_at(lookup, off)) / 255.0,
                 float(_byte_at(lookup, off + 1)) / 255.0,
                 float(_byte_at(lookup, off + 2)) / 255.0,
                 float(_byte_at(lookup, off + 3)) / 255.0)
        }
        else {
            rgb(float(_byte_at(lookup, off)) / 255.0,
                float(_byte_at(lookup, off + 1)) / 255.0,
                float(_byte_at(lookup, off + 2)) / 255.0)
        }
    }
}

fn _icc_component_count(cs) {
    if (cs is array and len(cs) >= 2 and cs[1] is map and cs[1].N != null) {
        if (cs[1].N is int) cs[1].N else if (cs[1].N is float) int(cs[1].N) else 3
    }
    else { 3 }
}

fn _from_component_count(nums, ncomp) {
    let n = len(nums)
    if (ncomp == 1 and n >= 1) { gray(nums[0]) }
    else if (ncomp == 4 and n >= 4) { cmyk(nums[0], nums[1], nums[2], nums[3]) }
    else if (n >= 3) { rgb(nums[0], nums[1], nums[2]) }
    else if (n >= 1) { gray(nums[0]) }
    else { BLACK }
}

pub fn from_ops_in_space(pdf, page, active_space, ops) {
    let nums = _numeric_ops(ops)
    let cs = _resolve_space(pdf, page, active_space)
    let t = _space_type(cs)
    if (t == "Indexed" or t == "I") {
        let v = if (len(nums) >= 1) nums[0] else 0
        _indexed_color(cs, v)
    }
    else if (t == "DeviceGray" or t == "G" or t == "CalGray") {
        _from_component_count(nums, 1)
    }
    else if (t == "DeviceCMYK" or t == "CMYK") {
        _from_component_count(nums, 4)
    }
    else if (t == "ICCBased") {
        _from_component_count(nums, _icc_component_count(cs))
    }
    else if (t == "Separation" or t == "DeviceN") {
        // Tint transforms are not interpreted yet; match the C++ view's
        // simple grayscale tint fallback.
        if (len(nums) >= 1) { gray(1.0 - _num(nums[0])) } else { BLACK }
    }
    else {
        _from_component_count(nums, 3)
    }
}

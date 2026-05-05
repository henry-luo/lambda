// pdf/shading.ls — PDF Shading (sh operator) → SVG gradient elements
//
// Phase 7 scope: minimal Type 2 (axial) and Type 3 (radial) shadings,
// using only the first and last colour in `/Function` if it's a sampled
// or stitching function. Anything more elaborate (function-based shading,
// triangle meshes, custom transfer curves) falls back to a flat fill of
// the average colour, or — if that can't be derived — to no-op.
//
// API:
//   shading.from_sh_op(pdf, page, ctm, ops, ctr) -> { defs: [...], emit: [...] }
//
// `defs` is a (possibly empty) list of <linearGradient>/<radialGradient>
// elements that must be inserted into the page's <defs>. `emit` is a
// (possibly empty) list of <rect>/<path> SVG elements painted with
// fill="url(#id)". `ctr` is a monotonic int the caller increments to
// keep gradient ids unique within a page.

import util:    .util
import resolve: .resolve
import color:   .color

fn _shading_dict(pdf, page, name) {
    let res = resolve.page_resources(pdf, page)
    let table = if (res and res.Shading) resolve.deref(pdf, res.Shading)
                else null
    if (table == null) { null }
    else { resolve.deref(pdf, table[name]) }
}

fn _component_color(vals, ncomp) {
    if (vals != null and ncomp == 3 and len(vals) >= 3) { color.rgb(vals[0], vals[1], vals[2]) }
    else if (vals != null and ncomp == 1 and len(vals) >= 1) { color.gray(vals[0]) }
    else { color.BLACK }
}

fn _function_start_color(pdf, func, ncomp) {
    let f = resolve.deref(pdf, func)
    if (f == null) { color.BLACK }
    else if (f.FunctionType == 3 and f.Functions and len(f.Functions) >= 1) {
        _function_start_color(pdf, f.Functions[0], ncomp)
    }
    else { _component_color(f.C0, ncomp) }
}

fn _function_end_color(pdf, func, ncomp) {
    let f = resolve.deref(pdf, func)
    if (f == null) { color.BLACK }
    else if (f.FunctionType == 3 and f.Functions and len(f.Functions) >= 1) {
        _function_end_color(pdf, f.Functions[len(f.Functions) - 1], ncomp)
    }
    else {
        let c = if (f.C1 != null) f.C1 else f.C0
        _component_color(c, ncomp)
    }
}

// Pull two endpoint colour stops from a /Function (Type 2 or stitching).
// Returns [color_start, color_end] each as an "rgb(...)" string, or
// [BLACK, BLACK] if introspection fails.
fn _endpoint_colors(pdf, func, ncomp) {
    // Best effort: only handle the common case where func.C0/C1 exist
    // (Type 2 exponential function used for a 2-stop axial gradient).
    [_function_start_color(pdf, func, ncomp), _function_end_color(pdf, func, ncomp)]
}

fn _stop_offset(v) {
    util.fmt_num(float(v) * 100.0) ++ "%"
}

fn _stitch_stop(pdf, func, ncomp, offset, use_end) {
    {
        offset: _stop_offset(offset),
        color: if (use_end) { _function_end_color(pdf, func, ncomp) }
               else { _function_start_color(pdf, func, ncomp) }
    }
}

fn _function_stops(pdf, func, ncomp) {
    let f = resolve.deref(pdf, func)
    if (f != null and f.FunctionType == 3 and f.Functions and len(f.Functions) >= 1) {
        let fs = f.Functions
        let bs = f.Bounds
        if (len(fs) == 2 and len(bs) >= 1) {
            [_stitch_stop(pdf, fs[0], ncomp, 0.0, false), _stitch_stop(pdf, fs[0], ncomp, bs[0], true),
             _stitch_stop(pdf, fs[1], ncomp, bs[0], false), _stitch_stop(pdf, fs[1], ncomp, 1.0, true)]
        }
        else if (len(fs) == 4 and len(bs) >= 3) {
            [_stitch_stop(pdf, fs[0], ncomp, 0.0, false), _stitch_stop(pdf, fs[0], ncomp, bs[0], true),
             _stitch_stop(pdf, fs[1], ncomp, bs[0], false), _stitch_stop(pdf, fs[1], ncomp, bs[1], true),
             _stitch_stop(pdf, fs[2], ncomp, bs[1], false), _stitch_stop(pdf, fs[2], ncomp, bs[2], true),
             _stitch_stop(pdf, fs[3], ncomp, bs[2], false), _stitch_stop(pdf, fs[3], ncomp, 1.0, true)]
        }
        else if (len(fs) == 5 and len(bs) >= 4) {
            [_stitch_stop(pdf, fs[0], ncomp, 0.0, false), _stitch_stop(pdf, fs[0], ncomp, bs[0], true),
             _stitch_stop(pdf, fs[1], ncomp, bs[0], false), _stitch_stop(pdf, fs[1], ncomp, bs[1], true),
             _stitch_stop(pdf, fs[2], ncomp, bs[1], false), _stitch_stop(pdf, fs[2], ncomp, bs[2], true),
             _stitch_stop(pdf, fs[3], ncomp, bs[2], false), _stitch_stop(pdf, fs[3], ncomp, bs[3], true),
             _stitch_stop(pdf, fs[4], ncomp, bs[3], false), _stitch_stop(pdf, fs[4], ncomp, 1.0, true)]
        }
        else { [_stitch_stop(pdf, fs[0], ncomp, 0.0, false), _stitch_stop(pdf, fs[len(fs) - 1], ncomp, 1.0, true)] }
    }
    else {
        let endpoints = _endpoint_colors(pdf, func, ncomp)
        [{ offset: "0%", color: endpoints[0] }, { offset: "100%", color: endpoints[1] }]
    }
}

fn _matrix_of(d) {
    let m = if (d) d.Matrix else null
    if (m is array and len(m) >= 6) {
        [float(m[0]), float(m[1]), float(m[2]), float(m[3]), float(m[4]), float(m[5])]
    }
    else { util.IDENTITY }
}

fn _matrix_inverse(m) {
    let det = m[0] * m[3] - m[1] * m[2]
    if (util.fabs(det) < 0.000001) { util.IDENTITY }
    else {
        let inv_det = 1.0 / det
        [
            m[3] * inv_det,
            (0.0 - m[1]) * inv_det,
            (0.0 - m[2]) * inv_det,
            m[0] * inv_det,
            (m[2] * m[5] - m[3] * m[4]) * inv_det,
            (m[1] * m[4] - m[0] * m[5]) * inv_det
        ]
    }
}

fn _matrix_point(m, x, y) {
    [x * m[0] + y * m[2] + m[4], x * m[1] + y * m[3] + m[5]]
}

fn _ncomp_for_cs(cs_name) {
    if (cs_name == "DeviceRGB" or cs_name == "RGB") { 3 }
    else if (cs_name == "DeviceCMYK" or cs_name == "CMYK") { 4 }
    else { 1 }
}

fn _cs_name(d) {
    let cs = if (d) d.ColorSpace else null
    if (cs is map and cs.kind == "name") { cs.value }
    else if (cs is string) { cs }
    else if (cs is array and len(cs) >= 1 and cs[0] == "ICCBased") { "DeviceRGB" }
    else { "DeviceRGB" }
}

// Build an <linearGradient> for a Type 2 shading, plus a covering <rect>
// painted with that gradient. The rect spans the page MediaBox in the
// caller's CTM; downstream the y-flip group inverts it.
fn _emit_axial_with_matrix(pdf, d, xform, id, page_w, page_h) {
    let coords = if (d.Coords) d.Coords else [0.0, 0.0, 1.0, 0.0]
    let p0 = _matrix_point(xform, float(coords[0]), float(coords[1]))
    let p1 = _matrix_point(xform, float(coords[2]), float(coords[3]))
    let x0 = p0[0]; let y0 = p0[1]
    let x1 = p1[0]; let y1 = p1[1]
    let func  = if (d.Function) d.Function else null
    let ncomp = _ncomp_for_cs(_cs_name(d))
    let stops = _function_stops(pdf, func, ncomp)
    let stop_els = for (s in stops) <stop offset: s.offset, 'stop-color': s.color>
    let grad = <linearGradient id: id, gradientUnits: "userSpaceOnUse",
                               x1: util.fmt_num(x0), y1: util.fmt_num(y0),
                               x2: util.fmt_num(x1), y2: util.fmt_num(y1);
                  for (el in stop_els) el
              >
    let cover = <rect x: "0", y: "0",
                      width: util.fmt_num(page_w),
                      height: util.fmt_num(page_h),
                      fill: "url(#" ++ id ++ ")">
    { defs: [grad], emit: [cover] }
}

fn _emit_axial(pdf, d, ctm, id, page_w, page_h) {
    _emit_axial_with_matrix(pdf, d, util.IDENTITY, id, page_w, page_h)
}

fn _emit_radial_with_matrix(pdf, d, xform, id, page_w, page_h) {
    let coords = if (d.Coords) d.Coords else [0.0, 0.0, 0.0, 0.0, 0.0, 1.0]
    let p0 = _matrix_point(xform, float(coords[0]), float(coords[1]))
    let p1 = _matrix_point(xform, float(coords[3]), float(coords[4]))
    let scale = (util.fabs(xform[0]) + util.fabs(xform[1]) +
                 util.fabs(xform[2]) + util.fabs(xform[3])) / 2.0
    let cx0 = p0[0]; let cy0 = p0[1]; let r0 = float(coords[2]) * scale
    let cx1 = p1[0]; let cy1 = p1[1]; let r1 = float(coords[5]) * scale
    let func  = if (d.Function) d.Function else null
    let ncomp = _ncomp_for_cs(_cs_name(d))
    let stops = _function_stops(pdf, func, ncomp)
    let stop_els = for (s in stops) <stop offset: s.offset, 'stop-color': s.color>
    let grad = <radialGradient id: id, gradientUnits: "userSpaceOnUse",
                               fx: util.fmt_num(cx0), fy: util.fmt_num(cy0),
                               cx: util.fmt_num(cx1), cy: util.fmt_num(cy1),
                               r:  util.fmt_num(r1);
                  for (el in stop_els) el
              >
    let cover = <rect x: "0", y: "0",
                      width: util.fmt_num(page_w),
                      height: util.fmt_num(page_h),
                      fill: "url(#" ++ id ++ ")">
    { defs: [grad], emit: [cover] }
}

fn _emit_radial(pdf, d, ctm, id, page_w, page_h) {
    _emit_radial_with_matrix(pdf, d, util.IDENTITY, id, page_w, page_h)
}

// Public entry. `ctr` is a monotonic counter (string) used to build the
// SVG id; the caller must pass distinct values per page. Returns
// { defs, emit } both possibly empty.
pub fn from_sh_op(pdf, page, ctm, ops, page_w, page_h, ctr) {
    let nm = if (len(ops) >= 1) util.name_of(ops[0]) else null
    if (nm == null) { { defs: [], emit: [] } }
    else {
        let d = _shading_dict(pdf, page, nm)
        if (d == null) { { defs: [], emit: [] } }
        else {
            let stype = if (d.ShadingType) d.ShadingType else 0
            let id = "shad" ++ string(ctr)
            if (stype == 2) { _emit_axial(pdf, d, ctm, id, page_w, page_h) }
            else if (stype == 3) { _emit_radial(pdf, d, ctm, id, page_w, page_h) }
            else { { defs: [], emit: [] } }
        }
    }
}

fn _pattern_table(pdf, page) {
    let res = resolve.page_resources(pdf, page)
    if (res.Pattern != null) { resolve.deref(pdf, res.Pattern) }
    else { null }
}

fn _pattern_dict(pdf, page, name) {
    let table = _pattern_table(pdf, page)
    if (table == null) { null }
    else { resolve.deref(pdf, table[name]) }
}

pub fn pattern_shading(pdf, page, name) {
    let p = _pattern_dict(pdf, page, name)
    if (p != null and p.PatternType == 2 and p.Shading != null) { resolve.deref(pdf, p.Shading) }
    else { null }
}

pub fn from_pattern_fill(pdf, page, name, id, ctm) {
    let p = _pattern_dict(pdf, page, name)
    let d = if (p != null and p.PatternType == 2 and p.Shading != null) { resolve.deref(pdf, p.Shading) }
            else { null }
    if (d == null) { { defs: [], fill: color.BLACK } }
    else {
        let xform = util.matrix_mul(_matrix_of(p), _matrix_inverse(ctm))
        let stype = if (d.ShadingType) d.ShadingType else 0
        let r = if (stype == 2) { _emit_axial_with_matrix(pdf, d, xform, id, 0.0, 0.0) }
                else if (stype == 3) { _emit_radial_with_matrix(pdf, d, xform, id, 0.0, 0.0) }
                else { { defs: [], emit: [] } }
        { defs: r.defs, fill: "url(#" ++ id ++ ")" }
    }
}

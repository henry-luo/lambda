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

fn _name_of(op) {
    if (op is map and op.kind == "name") { op.value } else { null }
}

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
fn _emit_axial(pdf, d, ctm, id, page_w, page_h) {
    let coords = if (d.Coords) d.Coords else [0.0, 0.0, 1.0, 0.0]
    let x0 = float(coords[0]); let y0 = float(coords[1])
    let x1 = float(coords[2]); let y1 = float(coords[3])
    let func  = if (d.Function) resolve.deref(pdf, d.Function) else null
    let ncomp = _ncomp_for_cs(_cs_name(d))
    let stops = _endpoint_colors(pdf, func, ncomp)
    let grad = <linearGradient id: id, gradientUnits: "userSpaceOnUse",
                               x1: util.fmt_num(x0), y1: util.fmt_num(y0),
                               x2: util.fmt_num(x1), y2: util.fmt_num(y1);
                  <stop offset: "0%",   'stop-color': stops[0]>
                  <stop offset: "100%", 'stop-color': stops[1]>
              >
    let cover = <rect x: "0", y: "0",
                      width: util.fmt_num(page_w),
                      height: util.fmt_num(page_h),
                      fill: "url(#" ++ id ++ ")">
    { defs: [grad], emit: [cover] }
}

fn _emit_radial(pdf, d, ctm, id, page_w, page_h) {
    let coords = if (d.Coords) d.Coords else [0.0, 0.0, 0.0, 0.0, 0.0, 1.0]
    let cx0 = float(coords[0]); let cy0 = float(coords[1]); let r0 = float(coords[2])
    let cx1 = float(coords[3]); let cy1 = float(coords[4]); let r1 = float(coords[5])
    let func  = if (d.Function) resolve.deref(pdf, d.Function) else null
    let ncomp = _ncomp_for_cs(_cs_name(d))
    let stops = _endpoint_colors(pdf, func, ncomp)
    let grad = <radialGradient id: id, gradientUnits: "userSpaceOnUse",
                               fx: util.fmt_num(cx0), fy: util.fmt_num(cy0),
                               cx: util.fmt_num(cx1), cy: util.fmt_num(cy1),
                               r:  util.fmt_num(r1);
                  <stop offset: "0%",   'stop-color': stops[0]>
                  <stop offset: "100%", 'stop-color': stops[1]>
              >
    let cover = <rect x: "0", y: "0",
                      width: util.fmt_num(page_w),
                      height: util.fmt_num(page_h),
                      fill: "url(#" ++ id ++ ")">
    { defs: [grad], emit: [cover] }
}

// Public entry. `ctr` is a monotonic counter (string) used to build the
// SVG id; the caller must pass distinct values per page. Returns
// { defs, emit } both possibly empty.
pub fn from_sh_op(pdf, page, ctm, ops, page_w, page_h, ctr) {
    let nm = if (len(ops) >= 1) _name_of(ops[0]) else null
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

pub fn pattern_shading(pdf, page, name) {
    let table = _pattern_table(pdf, page)
    if (table == null) { null }
    else {
        let p = resolve.deref(pdf, table[name])
        if (p != null and p.PatternType == 2 and p.Shading != null) { resolve.deref(pdf, p.Shading) }
        else { null }
    }
}

pub fn from_pattern_fill(pdf, page, name, id) {
    let d = pattern_shading(pdf, page, name)
    if (d == null) { { defs: [], fill: color.BLACK } }
    else {
        let stype = if (d.ShadingType) d.ShadingType else 0
        let r = if (stype == 2) { _emit_axial(pdf, d, util.IDENTITY, id, 0.0, 0.0) }
                else if (stype == 3) { _emit_radial(pdf, d, util.IDENTITY, id, 0.0, 0.0) }
                else { { defs: [], emit: [] } }
        { defs: r.defs, fill: "url(#" ++ id ++ ")" }
    }
}

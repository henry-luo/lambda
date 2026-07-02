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
import image:   .image

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

fn _sample_count(d) {
    if (d != null and d.Size is array and len(d.Size) >= 1) { util.int_or(d.Size[0], 0) }
    else { 0 }
}

fn _sample_range_value(d, comp, raw) {
    let r = if (d != null and d.Range is array) d.Range else null
    let lo = if (r != null and len(r) > (comp * 2)) { util.num(r[comp * 2]) } else { 0.0 }
    let hi = if (r != null and len(r) > (comp * 2 + 1)) { util.num(r[comp * 2 + 1]) } else { 1.0 }
    lo + ((hi - lo) * (float(raw) / 255.0))
}

fn _sampled_values(f, d, ncomp, sample_index) {
    let data = _stream_data(f)
    let off = sample_index * ncomp
    if (data == null or data == "") { [] }
    else {
        for (i in 0 to (ncomp - 1)) _sample_range_value(d, i, util.byte_at(data, off + i))
    }
}

fn _sampled_color(f, d, ncomp, sample_index) {
    _component_color(_sampled_values(f, d, ncomp, sample_index), ncomp)
}

fn _function_start_color(pdf, func, ncomp) {
    let f = resolve.deref(pdf, func)
    let d = _stream_dict(f)
    if (f == null) { color.BLACK }
    else if (d.FunctionType == 3 and d.Functions and len(d.Functions) >= 1) {
        _function_start_color(pdf, d.Functions[0], ncomp)
    }
    else if (d.FunctionType == 0 and d.BitsPerSample == 8) {
        _sampled_color(f, d, ncomp, 0)
    }
    else { _component_color(d.C0, ncomp) }
}

fn _function_end_color(pdf, func, ncomp) {
    let f = resolve.deref(pdf, func)
    let d = _stream_dict(f)
    if (f == null) { color.BLACK }
    else if (d.FunctionType == 3 and d.Functions and len(d.Functions) >= 1) {
        _function_end_color(pdf, d.Functions[len(d.Functions) - 1], ncomp)
    }
    else if (d.FunctionType == 0 and d.BitsPerSample == 8) {
        let samples = _sample_count(d)
        _sampled_color(f, d, ncomp, if (samples > 0) samples - 1 else 0)
    }
    else {
        let c = if (d.C1 != null) d.C1 else d.C0
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

fn _clamp_unit(v) {
    if (v < 0.0) { 0.0 }
    else if (v > 1.0) { 1.0 }
    else { v }
}

fn _alpha_at(alpha_mask, offset) {
    if (alpha_mask == null) { 1.0 }
    else { _clamp_unit(alpha_mask.a0 + ((alpha_mask.a1 - alpha_mask.a0) * offset)) }
}

fn _stop_elements(stops, alpha_mask) {
    if (alpha_mask == null) {
        for (s in stops) <stop offset: s.offset, 'stop-color': s.color>
    }
    else {
        for (s in stops) <stop offset: s.offset, 'stop-color': s.color,
                              'stop-opacity': util.fmt_num(_alpha_at(alpha_mask, s.value))>
    }
}

fn _stitch_stop(pdf, func, ncomp, offset, use_end) {
    {
        offset: _stop_offset(offset),
        value: offset,
        color: if (use_end) { _function_end_color(pdf, func, ncomp) }
               else { _function_start_color(pdf, func, ncomp) }
    }
}

fn _function_stops(pdf, func, ncomp) {
    let f = resolve.deref(pdf, func)
    let d = _stream_dict(f)
    if (f != null and d.FunctionType == 3 and d.Functions and len(d.Functions) >= 1) {
        let fs = d.Functions
        let bs = d.Bounds
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
        [{ offset: "0%", value: 0.0, color: endpoints[0] },
         { offset: "100%", value: 1.0, color: endpoints[1] }]
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

fn _component_count(vals) {
    if (vals is array and len(vals) >= 1) { len(vals) }
    else { 0 }
}

fn _infer_ncomp_from_function(pdf, func, fallback) {
    let f = resolve.deref(pdf, func)
    let d = _stream_dict(f)
    if (d == null) { fallback }
    else {
        let c0n = _component_count(d.C0)
        let c1n = _component_count(d.C1)
        let n = if (c0n > 0) { c0n }
                else if (c1n > 0) { c1n }
                else { fallback }
        if (n == 1 or n == 3 or n == 4) { n }
        else { fallback }
    }
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
fn _emit_axial_with_alpha_matrix(pdf, d, xform, id, page_w, page_h, alpha_mask) {
    let coords = if (d.Coords) d.Coords else [0.0, 0.0, 1.0, 0.0]
    let p0 = _matrix_point(xform, float(coords[0]), float(coords[1]))
    let p1 = _matrix_point(xform, float(coords[2]), float(coords[3]))
    let x0 = p0[0]; let y0 = p0[1]
    let x1 = p1[0]; let y1 = p1[1]
    let func  = if (d.Function) d.Function else null
    let cs_ncomp = _ncomp_for_cs(_cs_name(d))
    let ncomp = _infer_ncomp_from_function(pdf, func, cs_ncomp)
    let stops = _function_stops(pdf, func, ncomp)
    let stop_els = _stop_elements(stops, alpha_mask)
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

fn _emit_axial_with_matrix(pdf, d, xform, id, page_w, page_h) {
    _emit_axial_with_alpha_matrix(pdf, d, xform, id, page_w, page_h, null)
}

fn _emit_axial(pdf, d, ctm, id, page_w, page_h) {
    _emit_axial_with_matrix(pdf, d, util.IDENTITY, id, page_w, page_h)
}

fn _emit_radial_with_alpha_matrix(pdf, d, xform, id, page_w, page_h, alpha_mask) {
    let coords = if (d.Coords) d.Coords else [0.0, 0.0, 0.0, 0.0, 0.0, 1.0]
    let p0 = _matrix_point(xform, float(coords[0]), float(coords[1]))
    let p1 = _matrix_point(xform, float(coords[3]), float(coords[4]))
    let scale = (util.fabs(xform[0]) + util.fabs(xform[1]) +
                 util.fabs(xform[2]) + util.fabs(xform[3])) / 2.0
    let cx0 = p0[0]; let cy0 = p0[1]; let r0 = float(coords[2]) * scale
    let cx1 = p1[0]; let cy1 = p1[1]; let r1 = float(coords[5]) * scale
    let func  = if (d.Function) d.Function else null
    let cs_ncomp = _ncomp_for_cs(_cs_name(d))
    let ncomp = _infer_ncomp_from_function(pdf, func, cs_ncomp)
    let stops = _function_stops(pdf, func, ncomp)
    let stop_els = _stop_elements(stops, alpha_mask)
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

fn _emit_radial_with_matrix(pdf, d, xform, id, page_w, page_h) {
    _emit_radial_with_alpha_matrix(pdf, d, xform, id, page_w, page_h, null)
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
            let id = "shad" ++ (ctr)
            if (stype == 2) { _emit_axial(pdf, d, ctm, id, page_w, page_h) }
            else if (stype == 3) { _emit_radial(pdf, d, ctm, id, page_w, page_h) }
            else { { defs: [], emit: [] } }
        }
    }
}

fn _pattern_table(pdf, page) {
    let res = resolve.page_resources(pdf, page)
    let raw = res["Pattern"]
    if (raw is map and raw.type == "indirect_ref") { resolve.deref(pdf, raw) }
    else if (raw != null) { raw }
    else { null }
}

fn _pattern_dict(pdf, page, name) {
    let table = _pattern_table(pdf, page)
    if (table == null) { null }
    else { resolve.deref(pdf, table[name]) }
}

fn _stream_dict(s) {
    if (s != null and s.dictionary != null) { s.dictionary }
    else { s }
}

fn _stream_data(s) {
    if (s != null and s.data != null) { s.data }
    else if (s != null and s.stream_data != null) { s.stream_data }
    else { "" }
}

fn _page_media_box_value(page) {
    if (page and page.dict and page.dict.MediaBox) { page.dict.MediaBox }
    else if (page and page.MediaBox) { page.MediaBox }
    else { null }
}

fn _pattern_page(pdf, page, d) {
        let raw_res = if (d != null and d.Resources != null) { d.Resources } else { null }
        let res = if (raw_res is map and raw_res.type == "indirect_ref") { resolve.deref(pdf, raw_res) }
                            else if (raw_res != null) { raw_res }
                            else { resolve.page_resources(pdf, page) }
    { dict: { Resources: res, MediaBox: _page_media_box_value(page) },
      resources: res,
      MediaBox: _page_media_box_value(page) }
}

fn _pattern_bbox(d) {
    if (d != null and d.BBox is array and len(d.BBox) >= 4) { d.BBox }
    else { [0.0, 0.0, 1.0, 1.0] }
}

fn _pattern_xstep(d, bbox) {
    if (d != null and d.XStep != null) { util.num(d.XStep) }
    else { util.num(bbox[2]) - util.num(bbox[0]) }
}

fn _pattern_ystep(d, bbox) {
    if (d != null and d.YStep != null) { util.num(d.YStep) }
    else { util.num(bbox[3]) - util.num(bbox[1]) }
}

fn _do_name_from_operands(ops) {
    if (len(ops) < 1) { null }
    else {
        let op0 = ops[0]
        if (op0 is map and op0.kind == "name") { op0.value }
        else { null }
    }
}

fn _pop_ctm(stack, fallback) {
    let m = len(stack)
    if (m >= 1) {
        { ctm: stack[m - 1], stack: (for (k, v in stack where k < (m - 1)) v) }
    }
    else { { ctm: fallback, stack: stack } }
}

fn _rect_child(rect, fill, ctm) {
    let el = <rect x: util.fmt_num(rect.x), y: util.fmt_num(rect.y),
                   width: util.fmt_num(rect.w), height: util.fmt_num(rect.h),
                   fill: fill>
    <g transform: util.fmt_matrix(ctm); el>
}

fn _pattern_color_from_ops(pdf, page, operands, id, ctm, fallback) {
    let nm = _do_name_from_operands(operands)
    if (nm == null) { { defs: [], fill: fallback } }
    else { from_pattern_fill(pdf, page, nm, id, ctm) }
}

fn _tiling_pattern_children_loop(pdf, page, ops, i, n, ctm, stack, fill, defs, rect, out, id) {
    if (i >= n) { out }
    else {
        let op = ops[i]
        let opr = op.op
        let operands = op.operands
        if (opr == "q") {
            _tiling_pattern_children_loop(pdf, page, ops, i + 1, n, ctm, stack ++ [ctm], fill, defs, rect, out, id)
        }
        else if (opr == "Q") {
            let popped = _pop_ctm(stack, ctm)
            _tiling_pattern_children_loop(pdf, page, ops, i + 1, n, popped.ctm, popped.stack, fill, defs, rect, out, id)
        }
        else if (opr == "cm") {
            if (len(operands) >= 6) {
                let mtx = [util.num(operands[0]), util.num(operands[1]), util.num(operands[2]),
                           util.num(operands[3]), util.num(operands[4]), util.num(operands[5])]
                _tiling_pattern_children_loop(pdf, page, ops, i + 1, n, util.matrix_mul(mtx, ctm), stack, fill, defs, rect, out, id)
            }
            else { _tiling_pattern_children_loop(pdf, page, ops, i + 1, n, ctm, stack, fill, defs, rect, out, id) }
        }
        else if (opr == "g" and len(operands) >= 1) {
            _tiling_pattern_children_loop(pdf, page, ops, i + 1, n, ctm, stack, color.from_g_ops(operands), defs, rect, out, id)
        }
        else if (opr == "rg" and len(operands) >= 3) {
            _tiling_pattern_children_loop(pdf, page, ops, i + 1, n, ctm, stack, color.from_rg_ops(operands), defs, rect, out, id)
        }
        else if (opr == "k" and len(operands) >= 4) {
            _tiling_pattern_children_loop(pdf, page, ops, i + 1, n, ctm, stack, color.from_k_ops(operands), defs, rect, out, id)
        }
        else if (opr == "sc" or opr == "scn") {
            let pc = _pattern_color_from_ops(pdf, page, operands, id ++ "_nested" ++ (i), ctm, color.from_sc_ops(operands))
            _tiling_pattern_children_loop(pdf, page, ops, i + 1, n, ctm, stack, pc.fill, defs ++ pc.defs, rect, out, id)
        }
        else if (opr == "re" and len(operands) >= 4) {
            let r = { x: util.num(operands[0]), y: util.num(operands[1]),
                      w: util.num(operands[2]), h: util.num(operands[3]) }
            _tiling_pattern_children_loop(pdf, page, ops, i + 1, n, ctm, stack, fill, defs, r, out, id)
        }
        else if ((opr == "f" or opr == "F" or opr == "f*") and rect != null) {
            _tiling_pattern_children_loop(pdf, page, ops, i + 1, n, ctm, stack, fill, defs, null, out ++ [_rect_child(rect, fill, ctm)], id)
        }
        else if (opr == "Do" and _do_name_from_operands(operands) != null) {
            let imgs = image.apply_do(pdf, page, ctm, operands)
            _tiling_pattern_children_loop(pdf, page, ops, i + 1, n, ctm, stack, fill, defs, rect, out ++ (for (img in imgs) img), id)
        }
        else { _tiling_pattern_children_loop(pdf, page, ops, i + 1, n, ctm, stack, fill, defs, rect, out, id) }
    }
}

fn _tiling_pattern_children(pdf, page, ops, id) {
    _tiling_pattern_children_loop(pdf, page, ops, 0, len(ops), util.IDENTITY, [], color.BLACK, [], null, [], id)
}

fn _tiling_pattern_defs_loop(pdf, page, ops, i, n, ctm, stack, fill, defs, id) {
    if (i >= n) { defs }
    else {
        let op = ops[i]
        let opr = op.op
        let operands = op.operands
        if (opr == "q") { _tiling_pattern_defs_loop(pdf, page, ops, i + 1, n, ctm, stack ++ [ctm], fill, defs, id) }
        else if (opr == "Q") {
            let popped = _pop_ctm(stack, ctm)
            _tiling_pattern_defs_loop(pdf, page, ops, i + 1, n, popped.ctm, popped.stack, fill, defs, id)
        }
        else if (opr == "cm" and len(operands) >= 6) {
            let mtx = [util.num(operands[0]), util.num(operands[1]), util.num(operands[2]),
                       util.num(operands[3]), util.num(operands[4]), util.num(operands[5])]
            _tiling_pattern_defs_loop(pdf, page, ops, i + 1, n, util.matrix_mul(mtx, ctm), stack, fill, defs, id)
        }
        else if (opr == "scn") {
            let pc = _pattern_color_from_ops(pdf, page, operands, id ++ "_nested" ++ (i), ctm, fill)
            _tiling_pattern_defs_loop(pdf, page, ops, i + 1, n, ctm, stack, pc.fill, defs ++ pc.defs, id)
        }
        else { _tiling_pattern_defs_loop(pdf, page, ops, i + 1, n, ctm, stack, fill, defs, id) }
    }
}

fn _tiling_pattern_defs(pdf, page, ops, id) {
    _tiling_pattern_defs_loop(pdf, page, ops, 0, len(ops), util.IDENTITY, [], color.BLACK, [], id)
}

pub fn from_tiling_pattern_fill(pdf, page, p, id) {
    let d = _stream_dict(p)
    let bytes = _stream_data(p)
    if (d == null or bytes == "") { { defs: [], fill: color.BLACK } }
    else {
        let bbox = _pattern_bbox(d)
        let x = util.num(bbox[0])
        let y = util.num(bbox[1])
        let w = _pattern_xstep(d, bbox)
        let h = _pattern_ystep(d, bbox)
        if (w <= 0.0 or h <= 0.0) { { defs: [], fill: color.BLACK } }
        else {
            let m0 = _matrix_of(d)
            let m = [m0[0], m0[1], m0[2], m0[3], m0[4], m0[5] - h]
            let ppage = _pattern_page(pdf, page, d)
            let ops = pdf_parse_content_stream(bytes)
            let extra_defs = _tiling_pattern_defs(pdf, ppage, ops, id)
            let kids = _tiling_pattern_children(pdf, ppage, ops, id)
            if (len(kids) == 0) { { defs: [], fill: color.BLACK } }
            else {
                let pat = <pattern id: id,
                                   patternUnits: "userSpaceOnUse",
                                   x: util.fmt_num(x), y: util.fmt_num(y),
                                   width: util.fmt_num(w), height: util.fmt_num(h),
                                   patternTransform: util.fmt_matrix(m);
                              for (kid in kids) kid
                          >
                { defs: extra_defs ++ [pat], fill: "url(#" ++ id ++ ")" }
            }
        }
    }
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
    let pd = _stream_dict(p)
    if (p != null and pd != null and pd.PatternType == 1 and pd.PaintType == 1) { from_tiling_pattern_fill(pdf, page, p, id) }
    else if (d == null) { { defs: [], fill: color.BLACK } }
    else {
        let xform = util.matrix_mul(_matrix_of(p), _matrix_inverse(ctm))
        let stype = if (d.ShadingType) d.ShadingType else 0
        let r = if (stype == 2) { _emit_axial_with_matrix(pdf, d, xform, id, 0.0, 0.0) }
                else if (stype == 3) { _emit_radial_with_matrix(pdf, d, xform, id, 0.0, 0.0) }
                else { { defs: [], emit: [] } }
        { defs: r.defs, fill: "url(#" ++ id ++ ")" }
    }
}

pub fn from_pattern_fill_with_alpha(pdf, page, name, id, ctm, alpha_mask) {
    let p = _pattern_dict(pdf, page, name)
    let d = if (p != null and p.PatternType == 2 and p.Shading != null) { resolve.deref(pdf, p.Shading) }
            else { null }
    let pd = _stream_dict(p)
    if (p != null and pd != null and pd.PatternType == 1 and pd.PaintType == 1) { from_tiling_pattern_fill(pdf, page, p, id) }
    else if (d == null) { { defs: [], fill: color.BLACK } }
    else {
        let xform = util.matrix_mul(_matrix_of(p), _matrix_inverse(ctm))
        let stype = if (d.ShadingType) d.ShadingType else 0
        let r = if (stype == 2) { _emit_axial_with_alpha_matrix(pdf, d, xform, id, 0.0, 0.0, alpha_mask) }
                else if (stype == 3) { _emit_radial_with_alpha_matrix(pdf, d, xform, id, 0.0, 0.0, alpha_mask) }
                else { { defs: [], emit: [] } }
        { defs: r.defs, fill: "url(#" ++ id ++ ")" }
    }
}

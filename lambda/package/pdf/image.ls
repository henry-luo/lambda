// pdf/image.ls — PDF Image XObject placement (Do operator)
//
// Phase 4 scope:
//   - Recognize the `Do /Name` operator.
//   - Resolve the name in the page's /Resources/XObject dict.
//   - For Image XObjects, emit:
//       <g transform="matrix(<ctm>)">
//         <image href="img:<object_num>"
//                x="0" y="0" width="1" height="1"
//                preserveAspectRatio="none"
//                transform="matrix(1 0 0 -1 0 1)"/>
//       </g>
//     The `img:<num>` URL is a *handle*, not a data URI: the C side owns
//     the decoded bytes and the downstream renderer is expected to
//     resolve the handle via the original PDF's `objects` array.
//   - Form XObjects (Subtype = Form) are deferred to Phase 6; we emit a
//     placeholder <g data-pdf-form="img:<num>"/> so the SVG remains valid.
//
// CTM model:
//   PDF images are drawn in a unit square (0,0)→(1,1) in PDF user space;
//   the CTM holds the placement, scale, and rotation. We forward the
//   live CTM as the outer <g transform="...">. The inner inversion
//   `matrix(1 0 0 -1 0 1)` flips the unit square so the source pixels
//   (top-row-first) come out right-side-up after the page-level y-flip.
//
// Most helpers are `fn`; image emitters that reuse procedural inline-pixel
// rendering are `pn`. State is the interp graphics state — we only read
// st.ctm here, never mutate.

import util:    .util
import resolve: .resolve
import svg:     .svg

// ============================================================
// XObject lookup
// ============================================================

// Look up an XObject by name in the page's /Resources/XObject map.
// Returns the deref'd entry (a Map with `dict` + `data`) or null.
pub fn lookup_xobject(pdf, page, name) {
    let res = resolve.page_resources(pdf, page)
    let xo  = if (res and res.XObject) resolve.deref(pdf, res.XObject) else null
    if (xo == null) { null }
    else {
        let raw = xo[name]
        // The resources dict holds indirect refs to each XObject stream.
        // resolve.deref returns the stream object's `content` map (which
        // contains both `dictionary` and `data`).
        if (raw == null) { null } else { _resolve_xref(pdf, raw) }
    }
}

// Returns { kind: "image"|"form"|"other", obj_num: int, dict, raw }.
// `obj_num` is 0 when the XObject is inline (no indirect reference);
// callers should treat 0 as "unrenderable" for now.
fn _resolve_xref(pdf, raw) {
    if (raw is map and raw.type == "indirect_ref") {
        let num = raw.object_num
        let content = resolve.deref(pdf, raw)
        _classify(content, num, raw)
    }
    else {
        _classify(raw, 0, raw)
    }
}

fn _classify(content, obj_num, raw) {
    let dict = if (content and content.dictionary) content.dictionary else content
    let sub  = if (dict and dict.Subtype) dict.Subtype else null
    let kind = if (sub == "Image") "image"
               else if (sub == "Form") "form"
               else "other"
    let data_uri = if (content and content.data_uri) content.data_uri else null
    let data = _stream_data(content, dict)
    { kind: kind, obj_num: obj_num, dict: dict, raw: raw, data_uri: data_uri, data: data, content: content }
}

fn _stream_data(content, dict) {
    if (content and content.data != null) { content.data }
    else if (content and content.stream_data != null) { content.stream_data }
    else if (dict and dict.data != null) { dict.data }
    else if (dict and dict.stream_data != null) { dict.stream_data }
    else { "" }
}

fn _form_matrix(m) {
    if (m is array and len(m) == 6) {
        [util.num(m[0]), util.num(m[1]), util.num(m[2]),
         util.num(m[3]), util.num(m[4]), util.num(m[5])]
    }
    else { util.IDENTITY }
}

// Look up a Form XObject by name and return enough information to
// recursively interpret its content stream. Returns null when the name
// does not resolve to a Form XObject or its data is missing.
//
//   { data:   string,          // raw content-stream bytes
//     matrix: [a b c d e f],   // /Matrix or identity
//     dict:   Map }            // the Form's full dictionary
pub fn form_content(pdf, page, name) {
    let res = resolve.page_resources(pdf, page)
    let xo  = if (res and res.XObject) resolve.deref(pdf, res.XObject) else null
    if (xo == null) { null }
    else {
        let raw = xo[name]
        if (raw == null) { null }
        else {
            let content = resolve.deref(pdf, raw)
            let dict = if (content and content.dictionary) content.dictionary else content
            let sub  = if (dict and dict.Subtype) dict.Subtype else null
            if (sub != "Form") { null }
            else {
                let data = _stream_data(content, dict)
                let m = if (dict and dict.Matrix) dict.Matrix else null
                { data: data, matrix: _form_matrix(m), dict: dict }
            }
        }
    }
}

// ============================================================
// Emission
// ============================================================

fn _img_url(obj_num) {
    "img:" ++ string(obj_num)
}

// Emit a single <g><image/></g> for an Image XObject. obj_num=0 is dropped.
// `href_url` is the URL to use for the SVG <image href="..."/>: a real
// `data:image/png;base64,...` data URI when the C postprocess produced
// one, otherwise the legacy `img:<num>` handle (which downstream
// renderers may not resolve).
fn _emit_image(ctm, obj_num, href_url) {
    if (obj_num == 0 and href_url == null) { [] }
    else {
        let url = if (href_url != null) href_url else _img_url(obj_num)
        let outer = util.fmt_matrix(ctm)
        let elem =
            <g transform: outer;
                <image href: url,
                       x: "0", y: "0", width: "1", height: "1",
                       preserveAspectRatio: "none",
                       transform: "matrix(1 0 0 -1 0 1)">
            >
        [elem]
    }
}

fn _xobject_data(xo) {
    if (xo and xo.content and xo.content.data != null) { xo.content.data }
    else if (xo and xo.content and xo.content.stream_data != null) { xo.content.stream_data }
    else if (xo and xo.data != null) { xo.data }
    else if (xo and xo.dict and xo.dict.stream_data != null) { xo.dict.stream_data }
    else { "" }
}

fn _filter_name(f) {
    if (f is string) { f }
    else if (f is map and f.kind == "name") { f.value }
    else if (f is map and f.kind == "array" and len(f.value) >= 1) { _filter_name(f.value[0]) }
    else if (f is array and len(f) >= 1) { _filter_name(f[0]) }
    else { null }
}

fn _is_passthrough_filter(f) {
    let nm = _filter_name(f)
    (nm == "DCTDecode") or (nm == "DCT") or (nm == "JPXDecode")
}

fn _resource_color_space(pdf, page, name) {
    let res = resolve.page_resources(pdf, page)
    let table = if (res and res.ColorSpace) resolve.deref(pdf, res.ColorSpace) else null
    if (table == null) { null }
    else { resolve.deref(pdf, table[name]) }
}

fn _is_device_space_name(nm) {
    (nm == "DeviceRGB") or (nm == "RGB") or
    (nm == "DeviceGray") or (nm == "G") or
    (nm == "DeviceCMYK") or (nm == "CMYK")
}

fn _resolve_image_space_at(pdf, page, cs, depth) {
    let nm = util.name_of(cs)
    if (cs is array and len(cs) >= 4 and _is_indexed(cs)) {
        let base = _resolve_image_space_at(pdf, page, cs[1], depth)
        [cs[0], base, cs[2], cs[3]]
    }
    else if (nm == null) { cs }
    else if (_is_device_space_name(nm)) { nm }
    else if (depth <= 0) { cs }
    else {
        let found = _resource_color_space(pdf, page, nm)
        if (found == null) { cs } else { _resolve_image_space_at(pdf, page, found, depth - 1) }
    }
}

fn _resolve_image_space(pdf, page, cs) {
    _resolve_image_space_at(pdf, page, cs, 8)
}

fn _is_raw_xobject(pdf, page, dict, data) {
    if (dict == null or data == null or data == "") { false }
    else if (_is_passthrough_filter(dict.Filter)) { false }
    else {
        let w = util.int_or(dict.Width, 0)
        let h = util.int_or(dict.Height, 0)
        let bpc = util.int_or(dict.BitsPerComponent, 8)
        let raw_cs = if (dict.ColorSpace != null) { dict.ColorSpace } else { "DeviceRGB" }
        let cs = _resolve_image_space(pdf, page, raw_cs)
        let ncomp = _inline_ncomp(cs)
        let bad_size = ((w <= 0) or (h <= 0))
        let bad_format = (((bpc != 8) and (bpc != 1) and (bpc != 4)) or (ncomp == 0) or (((bpc == 1) or (bpc == 4)) and (ncomp != 1)))
        let too_large = ((w * h) > 4096)
        not (bad_size or bad_format or too_large)
    }
}

fn _xobject_inline_info(pdf, page, dict, data) {
    let raw_cs = if (dict.ColorSpace != null) { dict.ColorSpace } else { "DeviceRGB" }
    let cs = _resolve_image_space(pdf, page, raw_cs)
    {
        kind: "inline_image",
        dict: [
            { key: "W", value: dict.Width },
            { key: "H", value: dict.Height },
            { key: "CS", value: cs },
            { key: "BPC", value: util.int_or(dict.BitsPerComponent, 8) }
        ],
        data: data
    }
}

pn _emit_xobject_image(pdf, page, ctm, xo) {
    let data = _xobject_data(xo)
    if (xo.data_uri == null and _is_raw_xobject(pdf, page, xo.dict, data)) {
        return [svg.group(util.fmt_matrix(ctm), _emit_inline_pixels(_xobject_inline_info(pdf, page, xo.dict, data)))]
    }
    else { return _emit_image(ctm, xo.obj_num, xo.data_uri) }
}

// Emit a placeholder for a Form XObject (deferred to Phase 6).
fn _emit_form_stub(ctm, obj_num) {
    if (obj_num == 0) { [] }
    else {
        let outer = util.fmt_matrix(ctm)
        let elem =
            <g transform: outer, 'data-pdf-form': _img_url(obj_num);
                "form-xobject"
            >
        [elem]
    }
}

// Public: handle a Do operator. Returns a (possibly empty) list of SVG
// elements to append to the page's path layer (so they go inside the
// outer y-flip group alongside vector paths).
pub pn apply_do(pdf, page, ctm, ops) {
    if (len(ops) == 0) { return [] }
    else {
        let op0 = ops[0]
        if (op0 is map and op0.kind == "name") {
            let xo = lookup_xobject(pdf, page, op0.value)
            if (xo == null) { return [] }
            else if (xo.kind == "image") { return _emit_xobject_image(pdf, page, ctm, xo) }
            else if (xo.kind == "form")  { return _emit_form_stub(ctm, xo.obj_num) }
            else { return [] }
        }
        else { return [] }
    }
}

fn _resolve_inline_pair(pdf, page, p) {
    if ((p.key == "CS") or (p.key == "ColorSpace")) {
        { key: p.key, value: _resolve_image_space(pdf, page, p.value) }
    }
    else { p }
}

fn _inline_info_with_resources(pdf, page, info) {
    if ((info is map) and (info.kind == "inline_image") and (info.dict is array)) {
        { kind: "inline_image",
          dict: (for (p in info.dict) _resolve_inline_pair(pdf, page, p)),
          data: info.data }
    }
    else { info }
}

fn _placeholder_inline() {
    let elem =
        <rect x: "0", y: "0", width: "1", height: "1",
              fill: "rgba(200,200,200,0.4)",
              stroke: "rgb(160,160,160)",
              'stroke-width': "0.02",
              'stroke-dasharray': "0.05,0.05",
              transform: "matrix(1 0 0 -1 0 1)">
    [elem]
}

fn _space_type(cs) {
    if (cs is array and len(cs) >= 1) { util.name_of(cs[0]) }
    else if (cs is map and cs.N != null) { "ICCBased" }
    else if (cs is map) { "DeviceRGB" }
    else { util.name_of(cs) }
}

fn _is_indexed(cs) {
    let t = _space_type(cs)
    (t == "Indexed") or (t == "I")
}

fn _is_icc_based(cs) {
    _space_type(cs) == "ICCBased"
}

fn _icc_n_value(v) {
    let n = if (v is int) v else if (v is float) int(v) else 3
    if (n <= 0) { 3 } else { n }
}

fn _icc_ncomp(cs) {
    if (cs is map and cs.N != null) {
        _icc_n_value(cs.N)
    }
    else if (cs is array and len(cs) >= 2 and cs[1] is map and cs[1].N != null) {
        _icc_n_value(cs[1].N)
    }
    else { 3 }
}

fn _base_ncomp(base) {
    let t = _space_type(base)
    if ((t == "G") or (t == "DeviceGray") or (t == "CalGray")) { 1 }
    else if ((t == "CMYK") or (t == "DeviceCMYK")) { 4 }
    else if (t == "ICCBased") { _icc_ncomp(base) }
    else { 3 }
}

fn _inline_ncomp(cs) {
    let n = _space_type(cs)
    if ((n == "G") or (n == "DeviceGray") or (n == "CalGray")) { 1 }
    else if ((n == "RGB") or (n == "DeviceRGB") or (n == "CalRGB")) { 3 }
    else if ((n == "CMYK") or (n == "DeviceCMYK")) { 4 }
    else if (_is_indexed(cs)) { 1 }
    else if (_is_icc_based(cs)) { _icc_ncomp(cs) }
    else { 3 }
}

fn _div_int(a, b) {
    int(float(a) / float(b))
}

fn _row_bytes(w, ncomp, bpc) {
    _div_int((w * ncomp * bpc) + 7, 8)
}

fn _rgb_int(r, g, b) {
    "rgb(" ++ string(r) ++ "," ++ string(g) ++ "," ++ string(b) ++ ")"
}

fn _gamma_num(v, fallback) {
    if (v is int or v is float) { util.num(v) } else { fallback }
}

fn _cal_gray_gamma(cs) {
    if (cs is array and len(cs) >= 2 and cs[1] is map and cs[1].Gamma != null) {
        _gamma_num(cs[1].Gamma, 1.0)
    }
    else { 1.0 }
}

fn _cal_rgb_gamma(cs, index) {
    if (cs is array and len(cs) >= 2 and cs[1] is map and cs[1].Gamma != null) {
        let g = cs[1].Gamma
        if (g is array and len(g) > index) { _gamma_num(g[index], 1.0) }
        else { 1.0 }
    }
    else { 1.0 }
}

fn _gamma_byte(data, off, gamma) {
    let raw = float(util.byte_at(data, off)) / 255.0
    let v = if (gamma != 1.0 and raw > 0.0) { math.pow(raw, gamma) } else { raw }
    int(v * 255.0)
}

fn _cmyk_pixel_fill(data, off) {
    let c = float(util.byte_at(data, off)) / 255.0
    let m = float(util.byte_at(data, off + 1)) / 255.0
    let y = float(util.byte_at(data, off + 2)) / 255.0
    let k = float(util.byte_at(data, off + 3)) / 255.0
    let r = int((1.0 - c) * (1.0 - k) * 255.0)
    let g = int((1.0 - m) * (1.0 - k) * 255.0)
    let b = int((1.0 - y) * (1.0 - k) * 255.0)
    _rgb_int(r, g, b)
}

fn _gray4_pixel_fill(data, w, pixel_index) {
    let y = _div_int(pixel_index, w)
    let x = pixel_index - (y * w)
    let byte_idx = (y * _row_bytes(w, 1, 4)) + _div_int(x, 2)
    let high = ((x - (_div_int(x, 2) * 2)) == 0)
    let raw = util.byte_at(data, byte_idx)
    let val = if (high) { shr(raw, 4) } else { band(raw, 15) }
    let g = val * 17
    _rgb_int(g, g, g)
}

fn _indexed_index(data, bpc, pixel_index) {
    if (bpc == 8) { util.byte_at(data, pixel_index) }
    else if (bpc == 4) {
        let byte_idx = _div_int(pixel_index, 2)
        let high = ((pixel_index - (_div_int(pixel_index, 2) * 2)) == 0)
        let raw = util.byte_at(data, byte_idx)
        if (high) { shr(raw, 4) } else { band(raw, 15) }
    }
    else if (bpc == 1) {
        let byte_idx = _div_int(pixel_index, 8)
        let bit_off = 7 - (pixel_index - (_div_int(pixel_index, 8) * 8))
        band(shr(util.byte_at(data, byte_idx), bit_off), 1)
    }
    else { 0 }
}

fn _indexed_pixel_fill(data, bpc, pixel_index, cs) {
    if (not (_is_indexed(cs)) or len(cs) < 4) { _rgb_int(0, 0, 0) }
    else {
        let base = cs[1]
        let hival = if (cs[2] is int) cs[2] else if (cs[2] is float) int(cs[2]) else 0
        let idx0 = _indexed_index(data, bpc, pixel_index)
        let idx = if (idx0 > hival) hival else idx0
        let ncomp = _base_ncomp(base)
        let off = idx * ncomp
        let lookup = cs[3]
        if (ncomp == 1) {
            let g = util.byte_at(lookup, off)
            _rgb_int(g, g, g)
        }
        else if (ncomp == 4) {
            _cmyk_pixel_fill(lookup, off)
        }
        else {
            _rgb_int(util.byte_at(lookup, off), util.byte_at(lookup, off + 1), util.byte_at(lookup, off + 2))
        }
    }
}

fn _inline_pixel_fill(data, ncomp, bpc, w, pixel_index, cs) {
    let t = _space_type(cs)
    if (_is_indexed(cs)) {
        _indexed_pixel_fill(data, bpc, pixel_index, cs)
    }
    else if (_is_icc_based(cs) and ncomp != 1 and ncomp != 3 and ncomp != 4) {
        _rgb_int(0, 0, 0)
    }
    else if (bpc == 1) {
        let y = _div_int(pixel_index, w)
        let x = pixel_index - (y * w)
        let byte_idx = (y * _row_bytes(w, ncomp, bpc)) + _div_int(x, 8)
        let bit_off = 7 - (x - (_div_int(x, 8) * 8))
        let val = band(shr(util.byte_at(data, byte_idx), bit_off), 1)
        if (val == 1) { _rgb_int(255, 255, 255) } else { _rgb_int(0, 0, 0) }
    }
    else if (bpc == 4) {
        _gray4_pixel_fill(data, w, pixel_index)
    }
    else if (ncomp == 1) {
        let off = pixel_index * ncomp
        let g = if (t == "CalGray") { _gamma_byte(data, off, _cal_gray_gamma(cs)) } else { util.byte_at(data, off) }
        _rgb_int(g, g, g)
    }
    else if (ncomp == 4) {
        _cmyk_pixel_fill(data, pixel_index * ncomp)
    }
    else {
        let off = pixel_index * ncomp
        if (t == "CalRGB") {
            _rgb_int(_gamma_byte(data, off, _cal_rgb_gamma(cs, 0)),
                     _gamma_byte(data, off + 1, _cal_rgb_gamma(cs, 1)),
                     _gamma_byte(data, off + 2, _cal_rgb_gamma(cs, 2)))
        }
        else {
            _rgb_int(util.byte_at(data, off), util.byte_at(data, off + 1), util.byte_at(data, off + 2))
        }
    }
}

pn _emit_inline_pixels(info) {
    var w = 0
    var h = 0
    var bpc = 8
    var ncomp = 3
    var cs = "DeviceRGB"
    var has_filter = 0
    if ((info != null) and (info.dict is array)) {
        var di = 0
        let dn = len(info.dict)
        while (di < dn) {
            let p = info.dict[di]
            if ((p.key == "W") or (p.key == "Width")) { w = util.int_or(p.value, 0) }
            else if ((p.key == "H") or (p.key == "Height")) { h = util.int_or(p.value, 0) }
            else if ((p.key == "BPC") or (p.key == "BitsPerComponent")) { bpc = util.int_or(p.value, 8) }
            else if ((p.key == "CS") or (p.key == "ColorSpace")) {
                cs = p.value
                ncomp = _inline_ncomp(p.value)
            }
            else if (((p.key == "F") or (p.key == "Filter")) and _filter_name(p.value) != null) { has_filter = 1 }
            di = di + 1
        }
    }
    var data = ""
    if ((info != null) and (info.data != null)) { data = info.data }
    let bad_size = ((w <= 0) or (h <= 0))
    let bad_format = (((bpc != 8) and (bpc != 1) and (bpc != 4)) or (ncomp == 0) or (((bpc == 1) or (bpc == 4)) and (ncomp != 1)))
    let too_large = ((w * h) > 4096)
    if (bad_size or bad_format or too_large or has_filter == 1) {
        return _placeholder_inline()
    }
    else {
        var rects = []
        let rw = 1.0 / float(w)
        let rh = 1.0 / float(h)
        var y = 0
        while (y < h) {
            var x = 0
            while (x < w) {
                let pix = y * w + x
                let elem =
                    <rect x: util.fmt_num(float(x) * rw),
                          y: util.fmt_num(float(y) * rh),
                          width: util.fmt_num(rw),
                          height: util.fmt_num(rh),
                          fill: _inline_pixel_fill(data, ncomp, bpc, w, pix, cs),
                          stroke: "none">
                rects = rects ++ [elem]
                x = x + 1
            }
            y = y + 1
        }
        return [svg.group("matrix(1 0 0 -1 0 1)", rects)]
    }
}

// Public: handle a synthetic `inline_image` op produced by stream.ls for a
// BI..ID..EI segment. Supported unfiltered DeviceRGB/DeviceGray/DeviceCMYK images
// are rendered as SVG pixel rects in the local image unit square; unsupported
// inline images keep the visible placeholder.
pub pn apply_inline(ctm, ops) {
    var info = null
    if (len(ops) > 0) { info = ops[0] }
    if ((info is map) and (info.kind == "inline_image")) { return _emit_inline_pixels(info) }
    else { return _placeholder_inline() }
}

pub pn apply_inline_with_page(pdf, page, ctm, ops) {
    var info = null
    if (len(ops) > 0) { info = ops[0] }
    let resolved = _inline_info_with_resources(pdf, page, info)
    if ((resolved is map) and (resolved.kind == "inline_image")) { return _emit_inline_pixels(resolved) }
    else { return _placeholder_inline() }
}

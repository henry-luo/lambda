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
// All helpers are `fn`. State is the interp graphics state — we only
// read st.ctm here, never mutate.

import util:    .util
import resolve: .resolve

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
    { kind: kind, obj_num: obj_num, dict: dict, raw: raw }
}

// ============================================================
// Emission
// ============================================================

fn _img_url(obj_num) {
    "img:" ++ string(obj_num)
}

// Emit a single <g><image/></g> for an Image XObject. obj_num=0 is dropped.
fn _emit_image(ctm, obj_num) {
    if (obj_num == 0) { [] }
    else {
        let outer = util.fmt_matrix(ctm)
        let elem =
            <g transform: outer;
                <image href: _img_url(obj_num),
                       x: "0", y: "0", width: "1", height: "1",
                       preserveAspectRatio: "none",
                       transform: "matrix(1 0 0 -1 0 1)">
            >
        [elem]
    }
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
pub fn apply_do(pdf, page, ctm, ops) {
    if (len(ops) == 0) { [] }
    else {
        let op0 = ops[0]
        if (op0 is map and op0.kind == "name") {
            let xo = lookup_xobject(pdf, page, op0.value)
            if (xo == null) { [] }
            else if (xo.kind == "image") { _emit_image(ctm, xo.obj_num) }
            else if (xo.kind == "form")  { _emit_form_stub(ctm, xo.obj_num) }
            else { [] }
        }
        else { [] }
    }
}

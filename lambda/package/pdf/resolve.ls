// pdf/resolve.ls — Indirect-reference & page-tree access helpers
//
// Most heavy lifting is done in C (input-pdf-postprocess.cpp):
//   - root.objects   : flat array of indirect_object wrappers
//   - root.pages     : flat array of fully-resolved page Maps with inheritable
//                      Resources/MediaBox/CropBox/Rotate already merged in
//   - each Font dict carries `to_unicode: Map` when /ToUnicode resolved
//
// This module provides convenience accessors for the Lambda side.

// ============================================================
// Page accessors
// ============================================================

// Get the array of flattened page Maps. Returns [] if missing.
pub fn pages(pdf) {
    if (pdf and pdf.pages) pdf.pages else []
}

pub fn page_count(pdf) {
    len(pages(pdf))
}

// Get the i-th page (0-based). Returns null if out of range.
pub fn page_at(pdf, i: int) {
    let ps = pages(pdf);
    if i >= 0 and i < len(ps) { ps[i] } else { null }
}

// ============================================================
// Document metadata
// ============================================================

// Extract the trailer's Info dict (best-effort). Returns {} when absent.
pub fn metadata(pdf) {
    if (pdf and pdf.trailer and pdf.trailer.dictionary and
        pdf.trailer.dictionary.Info)
        pdf.trailer.dictionary.Info
    else
        {}
}

// ============================================================
// Indirect-reference resolution
// ============================================================

// Find an indirect_object wrapper by its object_num. Returns null when not
// found. O(n) — callers that need many lookups should cache results.
fn find_obj(pdf, num) {
    let objs = if (pdf and pdf.objects) pdf.objects else [];
    let hits = (for (o in objs where o.object_num == num) o);
    if len(hits) > 0 { hits[0] } else { null }
}

// Resolve an item that may be an indirect_ref Map. Returns the referenced
// object's `content` (or the content's `dictionary` for stream objects),
// or the original item if it isn't a ref.
pub fn deref(pdf, it) {
    if (it and it is map and it.type == "indirect_ref") {
        let obj = find_obj(pdf, it.object_num);
        if (obj != null) { obj.content } else { null }
    }
    else { it }
}

// ============================================================
// Page content streams
// ============================================================

fn _stream_bytes(s) {
    if (s and s.data) { s.data }
    else if (s and s.stream_data != null) { s.stream_data }
    else { "" }
}

// Concatenated content-stream bytes for a page. PDF's /Contents may be:
//   - a single indirect_ref to a stream
//   - an array of indirect_refs to streams (concatenated with newlines)
//   - inline (rare)
// Returns "" when no content is present.
pub fn page_content_bytes(pdf, page) {
    let cref = if (page and page.dict) page.dict.Contents else null;
    if cref == null { "" }
    else {
        if cref is array {
            // array of refs → concat each stream's data with "\n"
            let parts = (for (r in cref)
                (let s = deref(pdf, r), _stream_bytes(s)));
            parts | join("\n")
        }
        else {
            let s = deref(pdf, cref);
            _stream_bytes(s)
        }
    }
}

// Page resources dict, with indirect-ref resolved one level down.
// Returns {} when absent.
pub fn page_resources(pdf, page) {
    let r = if (page and page.dict and page.dict.Resources) page.dict.Resources
            else (if (page and page.resources) page.resources else null);
    if r == null { {} }
    else { deref(pdf, r) }
}

// Look up a font dict in the page's /Resources/Font/<name>.
// Returns null when the name is not declared.
pub fn page_font(pdf, page, font_name: string) {
    let res = page_resources(pdf, page);
    let fonts = if (res and res.Font) deref(pdf, res.Font) else null;
    if fonts == null { null }
    else { deref(pdf, fonts[font_name]) }
}

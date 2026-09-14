// pdf/coords.ls — Coordinate-space conversions
//
// PDF default user space:  origin at bottom-left, +y goes up.
// SVG default user space:  origin at top-left, +y goes down.
// We render each page into an <svg> whose viewBox matches the PDF media box
// in PDF units, then apply a top-level y-flip transform so subsequent CTM
// arithmetic can stay in PDF coordinates.

import util: .util

// ============================================================
// Media box helpers
// ============================================================

// PDF /MediaBox is [llx, lly, urx, ury]. Return {x, y, w, h} in PDF units.
// Falls back to US Letter (612 x 792) if the box is missing or malformed.
pub fn media_box_rect(page) {
    let mb = if (page and page.media_box) page.media_box else null;
    if mb and len(mb) >= 4 {
        let llx = float(mb[0]);
        let lly = float(mb[1]);
        let urx = float(mb[2]);
        let ury = float(mb[3]);
        { x: llx, y: lly, w: urx - llx, h: ury - lly }
    }
    else { { x: 0.0, y: 0.0, w: 612.0, h: 792.0 } }
}

// Build the SVG `viewBox` string for a page.
pub fn view_box_attr(page) {
    let r = media_box_rect(page);
    util.fmt_num(r.x) ++ " " ++ util.fmt_num(r.y) ++ " " ++
    util.fmt_num(r.w) ++ " " ++ util.fmt_num(r.h)
}

// ============================================================
// Y-flip
// ============================================================

// Top-level transform that converts PDF user space (y-up, origin bottom-left)
// to SVG user space (y-down, origin top-left) given the page height.
// For a page of height H, the matrix is [1 0 0 -1 0 H]:
//   x' = x
//   y' = H - y
pub fn y_flip_matrix(page_height) {
    [1.0, 0.0, 0.0, -1.0, 0.0, float(page_height)]
}

// Same, formatted as an SVG transform string.
pub fn y_flip_transform(page_height) {
    util.fmt_matrix(y_flip_matrix(page_height))
}

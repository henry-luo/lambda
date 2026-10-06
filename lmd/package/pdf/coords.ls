// pdf/coords.ls — Coordinate-space conversions
//
// PDF default user space:  origin at bottom-left, +y goes up.
// SVG default user space:  origin at top-left, +y goes down.
// PDF content is first mapped to SVG y-down coordinates. The visible page
// transform then applies the effective CropBox and clockwise /Rotate value.

import util: .util
import resolve: .resolve

// ============================================================
// Media box helpers
// ============================================================

// PDF /MediaBox is [llx, lly, urx, ury]. Return {x, y, w, h} in PDF units.
// Falls back to US Letter (612 x 792) if the box is missing or malformed.
fn _box_rect(box) {
    if (box != null and box is array and len(box) >= 4) {
        let x = float(box[0])
        let y = float(box[1])
        let w = float(box[2]) - x
        let h = float(box[3]) - y
        if (w > 0.0 and h > 0.0) { {x: x, y: y, w: w, h: h} }
        else { null }
    }
    else { null }
}

pub fn media_box_rect(page) {
    let box = _box_rect(if (page and page.media_box) page.media_box else null)
    if (box != null) box else {x: 0.0, y: 0.0, w: 612.0, h: 792.0}
}

// Build the SVG `viewBox` string for a page.
pub fn view_box_attr(page) {
    rect_view_box(media_box_rect(page))
}

pub fn rect_view_box(rect) {
    util.fmt_num(rect.x) ++ " " ++ util.fmt_num(rect.y) ++ " " ++
    util.fmt_num(rect.w) ++ " " ++ util.fmt_num(rect.h)
}

fn _effective_crop_box(page, pdf, media) {
    let raw = if (page and page.crop_box) resolve.deref(pdf, page.crop_box) else null
    let crop = _box_rect(raw)
    if (crop == null) media
    else {
        // PDF page boundaries outside MediaBox are reduced to their intersection.
        let left = if (crop.x > media.x) crop.x else media.x
        let bottom = if (crop.y > media.y) crop.y else media.y
        let right = if (crop.x + crop.w < media.x + media.w) crop.x + crop.w else media.x + media.w
        let top = if (crop.y + crop.h < media.y + media.h) crop.y + crop.h else media.y + media.h
        if (right > left and top > bottom)
            {x: left, y: bottom, w: right - left, h: top - bottom}
        else media
    }
}

fn _page_rotation(page) {
    let raw = util.int_or(if (page) page.rotate else null, 0)
    let normalized = raw - int(floor(float(raw) / 360.0)) * 360
    if (normalized == 90 or normalized == 180 or normalized == 270) normalized
    else 0
}

// Map the existing y-down page coordinates into a zero-origin visible viewport.
// SVG's positive quarter-turn is clockwise because its y axis points down.
pub fn page_geometry(page, pdf) {
    let raw_media = if (page and page.media_box) resolve.deref(pdf, page.media_box) else null
    let resolved_media = _box_rect(raw_media)
    let media = if (resolved_media != null) resolved_media else media_box_rect(page)
    let crop = _effective_crop_box(page, pdf, media)
    let rotation = _page_rotation(page)
    let crop_top = media.y + media.h - crop.y - crop.h
    let x = crop.x
    let y = crop_top
    let w = crop.w
    let h = crop.h
    let matrix = if (rotation == 90) [0.0, 1.0, -1.0, 0.0, h + y, -x]
        else if (rotation == 180) [-1.0, 0.0, 0.0, -1.0, w + x, h + y]
        else if (rotation == 270) [0.0, -1.0, 1.0, 0.0, -y, w + x]
        else [1.0, 0.0, 0.0, 1.0, -x, -y]
    let turn = rotation == 90 or rotation == 270
    let viewport = if (turn) {x: 0.0, y: 0.0, w: h, h: w}
                   else {x: 0.0, y: 0.0, w: w, h: h}
    let transformed = rotation != 0 or crop.x != media.x or crop.y != media.y or
        crop.w != media.w or crop.h != media.h or media.x != 0.0 or media.y != 0.0
    {media: media, crop: crop, viewport: viewport, matrix: matrix, transformed: transformed}
}

// ============================================================
// Y-flip
// ============================================================

// Convert PDF user space (y-up) to SVG space (y-down) using the media top.
// For a page top T, the matrix is [1 0 0 -1 0 T]:
//   x' = x
//   y' = T - y
pub fn y_flip_matrix(page_top) {
    [1.0, 0.0, 0.0, -1.0, 0.0, float(page_top)]
}

// Same, formatted as an SVG transform string.
pub fn y_flip_transform(page_top) {
    util.fmt_matrix(y_flip_matrix(page_top))
}

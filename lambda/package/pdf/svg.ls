// pdf/svg.ls — SVG element construction helpers for the PDF package
//
// Thin wrappers over the Lambda element-literal syntax that fix the SVG
// namespace and provide convenient builders for the elements the PDF
// interpreter emits (group, path, text, image, clipPath, use, symbol).

import util: .util

// ============================================================
// Root <svg> element for one page
// ============================================================

pub fn svg_root(view_box: string, width, height, children) {
    <svg xmlns: "http://www.w3.org/2000/svg",
         viewBox: view_box,
         width: util.fmt_num(width), height: util.fmt_num(height);
        for (c in children) c
    >
}

// ============================================================
// Building blocks
// ============================================================

pub fn group(xform: string, children) {
    <g transform: xform;
        for (c in children) c
    >
}

pub fn group_class(cls: string, children) {
    <g class: cls;
        for (c in children) c
    >
}

pub fn path(d: string, fill: string, stroke: string, stroke_width) {
    <path d: d, fill: fill, stroke: stroke,
          'stroke-width': util.fmt_num(stroke_width)>
}

pub fn text_run(xform: string, font_family: string, font_size,
                fill: string, content) {
    <text transform: xform,
          'font-family': font_family,
          'font-size':   util.fmt_num(font_size),
          fill: fill;
        content
    >
}

// Background rectangle for a page, in PDF user space.
pub fn page_background(rect, fill: string) {
    <rect x: util.fmt_num(rect.x), y: util.fmt_num(rect.y),
          width: util.fmt_num(rect.w), height: util.fmt_num(rect.h),
          fill: fill>
}

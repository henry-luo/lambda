// chart/svg.ls — SVG element construction helpers
// Provides convenience functions for building SVG elements.

import util: .lambda.package.chart.util

// ============================================================
// SVG root element
// ============================================================

pub fn svg_root(width: int, height: int, children) {
    <svg xmlns: "http://www.w3.org/2000/svg",
         width: width, height: height,
         viewBox: "0 0 " ++ string(width) ++ " " ++ string(height);
        for (child in children) child
    >
}

// ============================================================
// Basic SVG elements
// ============================================================

pub fn rect(x, y, w, h, fill) {
    <rect x: x, y: y, width: w, height: h, fill: fill>
}

pub fn circle(cx, cy, r, fill) {
    <circle cx: cx, cy: cy, r: r, fill: fill>
}

pub fn line(x1, y1, x2, y2, stroke, stroke_width) {
    <line x1: x1, y1: y1, x2: x2, y2: y2,
          stroke: stroke, "stroke-width": stroke_width>
}

pub fn text_el(x, y, content) {
    <text x: x, y: y; content>
}

pub fn path_el(d: string) {
    <path d: d>
}

pub fn group(xform: string, children) {
    (if (xform)
        <g transform: xform;
            for (child in children) child
        >
    else
        <g;
            for (child in children) child
        >)
}

pub fn group_class(cls: string, children) {
    <g class: cls;
        for (child in children) child
    >
}

// ============================================================
// SVG path helpers
// ============================================================

// move to
pub fn M(x, y) string => "M" ++ util.fmt_num(x) ++ " " ++ util.fmt_num(y)

// line to
pub fn L(x, y) string => "L" ++ util.fmt_num(x) ++ " " ++ util.fmt_num(y)

// close path
pub fn Z_cmd() { "Z" }

// cubic bezier
pub fn C(x1, y1, x2, y2, x, y) string {
    "C" ++ util.fmt_num(x1) ++ " " ++ util.fmt_num(y1) ++ " "
        ++ util.fmt_num(x2) ++ " " ++ util.fmt_num(y2) ++ " "
        ++ util.fmt_num(x) ++ " " ++ util.fmt_num(y)
}

// arc command
pub fn A(rx, ry, rotation, large_arc, sweep, x, y) string {
    "A" ++ util.fmt_num(rx) ++ " " ++ util.fmt_num(ry) ++ " "
        ++ string(rotation) ++ " "
        ++ string(large_arc) ++ " " ++ string(sweep) ++ " "
        ++ util.fmt_num(x) ++ " " ++ util.fmt_num(y)
}

// build a path string from a list of points using line segments
pub fn line_path(points) string {
    if len(points) == 0 { "" }
    else {
        let first = points[0];
        let start = M(first[0], first[1]);
        let segments = (for (i in 1 to (len(points) - 1))
            L(points[i][0], points[i][1]));
        start ++ " " ++ (segments | str_join(" "))
    }
}

// build a closed area path: line along top, then line back along bottom
pub fn area_path(top_points, bottom_points) string {
    if len(top_points) == 0 { "" }
    else {
        let first = top_points[0];
        let bottom_rev = reverse(bottom_points);

        let d1 = M(first[0], first[1]);
        let top_segs = (for (i in 1 to (len(top_points) - 1))
            L(top_points[i][0], top_points[i][1]));
        let d2 = d1 ++ " " ++ (top_segs | str_join(" "));
        let bottom_segs = (for (bp in bottom_rev) L(bp[0], bp[1]));
        let d3 = d2 ++ " " ++ (bottom_segs | str_join(" "));
        d3 ++ " " ++ Z_cmd()
    }
}

// build an arc path segment for pie/donut charts
pub fn arc_path(cx, cy, inner_r, outer_r, start_angle, end_angle) string {
    let cos_s = cos(start_angle);
    let sin_s = sin(start_angle);
    let cos_e = cos(end_angle);
    let sin_e = sin(end_angle);
    let large = if (end_angle - start_angle > util.PI) 1 else 0;

    let ox1 = cx + outer_r * cos_s;
    let oy1 = cy + outer_r * sin_s;
    let ox2 = cx + outer_r * cos_e;
    let oy2 = cy + outer_r * sin_e;

    if inner_r > 0.0 {
        // donut: outer arc, line to inner, inner arc (reverse), close
        let ix1 = cx + inner_r * cos_e;
        let iy1 = cy + inner_r * sin_e;
        let ix2 = cx + inner_r * cos_s;
        let iy2 = cy + inner_r * sin_s;
        M(ox1, oy1) ++ " "
            ++ A(outer_r, outer_r, 0, large, 1, ox2, oy2) ++ " "
            ++ L(ix1, iy1) ++ " "
            ++ A(inner_r, inner_r, 0, large, 0, ix2, iy2) ++ " "
            ++ Z_cmd()
    }
    else {
        // pie: move to center, line to edge, arc, close
        M(cx, cy) ++ " "
            ++ L(ox1, oy1) ++ " "
            ++ A(outer_r, outer_r, 0, large, 1, ox2, oy2) ++ " "
            ++ Z_cmd()
    }
}

// ============================================================
// Transform string helpers
// ============================================================

pub fn translate(x, y) string => "translate(" ++ util.fmt_num(x) ++ ", " ++ util.fmt_num(y) ++ ")"

pub fn rotate(angle, cx, cy) string {
    "rotate(" ++ util.fmt_num(angle) ++ ", " ++ util.fmt_num(cx) ++ ", " ++ util.fmt_num(cy) ++ ")"
}

// chart/svg.ls — SVG element construction helpers
// Provides convenience functions for building SVG elements.

import util: .util

// ============================================================
// SVG root element
// ============================================================

pub fn svg_root(width, height, children, attrs = {}) {
    <svg *:attrs, xmlns: "http://www.w3.org/2000/svg",
         width: width, height: height,
         viewBox: "0 0 " ++ (width) ++ " " ++ (height),
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

// Symbol dimensions preserve encoded area instead of treating size as a radius.
pub fn symbol_mark(shape, x, y, area, attrs = {}, children = []) {
    let size = max([0.0, float(area)]);
    if (shape == "square") {
        let side = math.sqrt(size);
        <rect x: x - side / 2.0, y: y - side / 2.0, width: side, height: side, *:attrs,
            for (child in children) child>
    } else if (shape == "diamond" or shape == "triangle-up" or shape == "triangle-down" or shape == "cross") {
        let points = if (shape == "diamond") (
            let radius = math.sqrt(size / 2.0),
            [[x, y - radius], [x + radius, y], [x, y + radius], [x - radius, y]])
        else if (shape == "cross") (
            let unit = math.sqrt(size / 5.0) / 2.0,
            [for (p in [[-1,-3],[1,-3],[1,-1],[3,-1],[3,1],[1,1],[1,3],[-1,3],[-1,1],[-3,1],[-3,-1],[-1,-1]])
                [x + p[0] * unit, y + p[1] * unit]])
        else (
            let side = math.sqrt(4.0 * size / math.sqrt(3.0)),
            let height = side * math.sqrt(3.0) / 2.0,
            let sign = if (shape == "triangle-down") -1.0 else 1.0,
            [[x, y - sign * height * 2.0 / 3.0], [x + side / 2.0, y + sign * height / 3.0],
                [x - side / 2.0, y + sign * height / 3.0]]);
        <path d: line_path(points) ++ " Z", *:attrs, for (child in children) child>
    } else <circle cx: x, cy: y, r: math.sqrt(size / util.PI), *:attrs,
        for (child in children) child>
}

pub fn line(x1, y1, x2, y2, stroke, stroke_width) {
    <line x1: x1, y1: y1, x2: x2, y2: y2,
          stroke: stroke, 'stroke-width': stroke_width>
}

pub fn text_el(x, y, content) {
    <text x: x, y: y, content>
}

pub fn path_el(d: string) {
    <path d: d>
}

pub fn group(xform: string, children) {
    (if (xform)
        <g transform: xform,
            for (child in children) child
        >
    else
        <g
            for (child in children) child
        >)
}

pub fn group_class(cls: string, children) {
    <g class: cls,
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
    "C" ++ util.fmt_num(x1) ++ " " ++ util.fmt_num(y1) ++ " " ++ util.fmt_num(x2) ++ " " ++ util.fmt_num(y2) ++ " " ++ util.fmt_num(x) ++ " " ++ util.fmt_num(y)
}

// arc command
pub fn A(rx, ry, rotation, large_arc, sweep, x, y) string {
    "A" ++ util.fmt_num(rx) ++ " " ++ util.fmt_num(ry) ++ " " ++ (rotation) ++ " " ++ (large_arc) ++ " " ++ (sweep) ++ " " ++ util.fmt_num(x) ++ " " ++ util.fmt_num(y)
}

// build a path string from a list of points using line segments
pub fn line_path(points, interpolate = null) string {
    if len(points) == 0 { "" }
    else {
        let first = points[0];
        let start = M(first[0], first[1]);
        let segments = [for (i in 1 to (len(points) - 1)) (
            let previous = points[i - 1],
            let current = points[i],
            if (interpolate == "step-before") L(previous[0], current[1]) ++ " " ++ L(current[0], current[1])
            else if (interpolate == "step-after") L(current[0], previous[1]) ++ " " ++ L(current[0], current[1])
            else if (interpolate == "step") (
                let middle = (previous[0] + current[0]) / 2.0,
                L(middle, previous[1]) ++ " " ++ L(middle, current[1]) ++ " " ++ L(current[0], current[1]))
            else if (interpolate == "cardinal" or interpolate == "catmull-rom") (
                let before = points[max([0, i - 2])],
                let after = points[min([len(points) - 1, i + 1])],
                C(previous[0] + (current[0] - before[0]) / 6.0, previous[1] + (current[1] - before[1]) / 6.0,
                    current[0] - (after[0] - previous[0]) / 6.0, current[1] - (after[1] - previous[1]) / 6.0,
                    current[0], current[1]))
            else L(current[0], current[1]))];
        start ++ " " ++ (segments |> join(" "))
    }
}

pub fn arrow_head(x1, y1, x2, y2, color, length = 8.0, notched = false, half_width = null, curve = null) {
    let dx = x2 - x1
    let dy = y2 - y1
    let distance = math.sqrt(dx * dx + dy * dy)
    if (distance == 0 or length <= 0) null else {
        let ux = dx / distance
        let uy = dy / distance
        let base_x = x2 - ux * length
        let base_y = y2 - uy * length
        let wing = if (half_width != null) half_width else length * 0.375;
        let centers = if (curve != null) curve else [[base_x, base_y], [x2, y2]];
        let lengths = [for (i in 1 to (len(centers) - 1)) math.sqrt(
            (centers[i][0] - centers[i - 1][0]) ** 2 + (centers[i][1] - centers[i - 1][1]) ** 2)];
        let total = sum(lengths);
        // offset the actual curve and taper by arc length so the head meets a curved shaft centrally.
        let edges = [for (side in [1.0, -1.0]) [for (i in 0 to (len(centers) - 2),
            let before = centers[max(0, i - 1)], let after = centers[i + 1],
            let dx = after[0] - before[0], let dy = after[1] - before[1],
            let distance = math.sqrt(dx * dx + dy * dy),
            let width = wing * (1.0 - sum(slice(lengths, 0, i)) / total))
            [centers[i][0] + side * width * (if (distance > 0) dy / distance else uy),
                centers[i][1] - side * width * (if (distance > 0) dx / distance else ux)]]];
        <path d: line_path([[x2, y2], *reverse(edges[0]),
            *if (notched) [[x2 - ux * length * 0.7, y2 - uy * length * 0.7]] else [], *edges[1]]) ++ " Z", fill: color>
    }
}

// build a closed area path: line along top, then line back along bottom
pub fn area_path(top_points, bottom_points, interpolate = null) string {
    if len(top_points) == 0 { "" }
    else {
        let bottom_rev = reverse(bottom_points);
        let bottom_path = trim(line_path(bottom_rev, interpolate));
        line_path(top_points, interpolate) ++ " L" ++ slice(bottom_path, 1, len(bottom_path)) ++ " Z"
    }
}

// build an arc path segment for pie/donut charts
pub fn arc_path(cx, cy, inner_r, outer_r, start_angle, end_angle, rounded = false) string {
    let span = end_angle - start_angle;
    let sweep = if (span >= 0.0) 1 else 0;
    if (rounded and span == 0.0) ""
    else if (abs(span) >= util.TAU) {
        // SVG cannot express a full circle with one arc whose endpoints coincide.
        let ox = cx + outer_r * math.cos(start_angle);
        let oy = cy + outer_r * math.sin(start_angle);
        let outer = M(ox, oy) ++ " " ++ A(outer_r, outer_r, 0, 0, sweep, 2.0 * cx - ox, 2.0 * cy - oy) ++
            " " ++ A(outer_r, outer_r, 0, 0, sweep, ox, oy) ++ " Z";
        if (inner_r <= 0) outer else (
            let ix = cx + inner_r * math.cos(start_angle), let iy = cy + inner_r * math.sin(start_angle),
            outer ++ " " ++ M(ix, iy) ++ " " ++ A(inner_r, inner_r, 0, 0, 1 - sweep, 2.0 * cx - ix, 2.0 * cy - iy) ++
                " " ++ A(inner_r, inner_r, 0, 0, 1 - sweep, ix, iy) ++ " Z")
    } else {
        let cos_s = math.cos(start_angle);
        let sin_s = math.sin(start_angle);
        let cos_e = math.cos(end_angle);
        let sin_e = math.sin(end_angle);
        let large = if (abs(span) > util.PI) 1 else 0;

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
            let cap = (outer_r - inner_r) / 2.0;
            // one closed path keeps gradient coordinates continuous across both rounded caps.
            let p1 = M(ox1, oy1) ++ " " ++ A(outer_r, outer_r, 0, large, sweep, ox2, oy2);
            let p2 = p1 ++ " " ++ (if (rounded) A(cap, cap, 0, 0, sweep, ix1, iy1) else L(ix1, iy1)) ++
                " " ++ A(inner_r, inner_r, 0, large, 1 - sweep, ix2, iy2);
            p2 ++ (if (rounded) " " ++ A(cap, cap, 0, 0, sweep, ox1, oy1) else "") ++ " " ++ Z_cmd()
        }
        else {
            // pie: move to center, line to edge, arc, close
            let p1 = M(cx, cy) ++ " " ++ L(ox1, oy1);
            let p2 = p1 ++ " " ++ A(outer_r, outer_r, 0, large, sweep, ox2, oy2);
            p2 ++ " " ++ Z_cmd()
        }
    }
}

// ============================================================
// Transform string helpers
// ============================================================

pub fn translate(x, y) string => "translate(" ++ util.fmt_num(x) ++ ", " ++ util.fmt_num(y) ++ ")"

pub fn rotate(angle, cx, cy) string {
    "rotate(" ++ util.fmt_num(angle) ++ ", " ++ util.fmt_num(cx) ++ ", " ++ util.fmt_num(cy) ++ ")"
}

// ============================================================
// SVG defs, gradients, and clip paths
// ============================================================

pub fn defs(children) {
    <defs
        for (child in children) child
    >
}

fn gradient_stops(stops) => [for (s in stops)
    <stop offset: s.offset, 'stop-color': s.color, 'stop-opacity': if (s.opacity != null) s.opacity else 1.0>]

pub fn linear_gradient(id: string, x1, y1, x2, y2, stops, attrs = {}) =>
    <linearGradient *:attrs, id: id, x1: x1, y1: y1, x2: x2, y2: y2,
        for (el in gradient_stops(stops)) el>

pub fn radial_gradient(id: string, cx, cy, radius, fx, fy, inner_radius, stops, attrs = {}) =>
    <radialGradient *:attrs, id: id, cx: cx, cy: cy, r: radius, fx: fx, fy: fy, fr: inner_radius,
        for (el in gradient_stops(stops)) el>

pub fn clip_path_el(id: string, children) {
    <clipPath id: id,
        for (child in children) child
    >
}

// One reconstruction seam keeps immutable SVG projections independent of literal tag syntax.
pub fn rebuild(tag, attrs, children) {
    if (tag == 'g') <g *:attrs, *children>
    else if (tag == 'svg') <svg *:attrs, *children>
    else if (tag == 'path') <path *:attrs, *children>
    else if (tag == 'rect') <rect *:attrs, *children>
    else if (tag == 'circle') <circle *:attrs, *children>
    else if (tag == 'ellipse') <ellipse *:attrs, *children>
    else if (tag == 'line') <line *:attrs, *children>
    else if (tag == 'text') <text *:attrs, *children>
    else if (tag == 'tspan') <tspan *:attrs, *children>
    else if (tag == 'image') <image *:attrs, *children>
    else if (tag == 'title') <title *:attrs, *children>
    else if (tag == 'defs') <defs *:attrs, *children>
    else if (tag == 'clipPath') <clipPath *:attrs, *children>
    else if (tag == 'linearGradient') <linearGradient *:attrs, *children>
    else if (tag == 'radialGradient') <radialGradient *:attrs, *children>
    else if (tag == 'stop') <stop *:attrs, *children>
    else if (tag == 'pattern') <pattern *:attrs, *children>
    else if (tag == 'polygon') <polygon *:attrs, *children>
    else if (tag == 'polyline') <polyline *:attrs, *children>
    else error("chart: unsupported SVG reconstruction tag " ++ string(tag))
}

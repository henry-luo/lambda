// chart/annotation.ls — Annotation layer for the chart library
// Renders positioned annotations (text labels, reference lines) on charts.

import svg: .svg
import scale: .scale

// ============================================================
// Render all annotations from the annotation element
// ============================================================

pub fn render_annotations(annotation_el, x_scale, y_scale, plot_w, plot_h, theme) {
    let count = len(annotation_el)
    let elements = (for (i in 0 to (count - 1),
                         let child = annotation_el[i]
                         where child != null)
        render_one(child, x_scale, y_scale, plot_w, plot_h, theme)
    ) that (~ != null);
    svg.group_class("annotations", elements)
}

// ============================================================
// Dispatch single annotation
// ============================================================

fn render_one(note, x_scale, y_scale, plot_w, plot_h, theme) {
    let tag = name(note)
    if (tag == 'text_note') render_text_note(note, x_scale, y_scale, theme)
    else if (tag == 'rule_note') render_rule_note(note, x_scale, y_scale, plot_w, plot_h)
    else null
}

// ============================================================
// Text annotation at a data position
// ============================================================

fn render_text_note(note, x_scale, y_scale, theme) {
    let x = if (note.x != null and x_scale)
        float(scale.scale_apply(x_scale, note.x))
    else if (note.px != null) float(note.px)
    else 0.0
    let y = if (note.y != null and y_scale)
        float(scale.scale_apply(y_scale, note.y))
    else if (note.py != null) float(note.py)
    else 0.0
    let text_str = if (note.text) note.text else ""
    let color = if (note.color) note.color else theme.title_color
    let font_size = if (note.font_size) note.font_size else 11
    let anchor = if (note.anchor) note.anchor else "start"
    let dy = if (note.dy) note.dy else 0
    let dx = if (note.dx) note.dx else 0
    <text x: float(x) + float(dx), y: float(y) + float(dy),
          'text-anchor': anchor,
          'font-size': font_size,
          fill: color;
        text_str
    >
}

// ============================================================
// Rule annotation (horizontal or vertical reference line)
// ============================================================

fn render_rule_note(note, x_scale, y_scale, plot_w, plot_h) {
    let color = if (note.color) note.color else "#888"
    let stroke_w = if (note.stroke_width) note.stroke_width else 1.0
    let dash = if (note.stroke_dash) note.stroke_dash else null
    let is_h = note.y != null and y_scale
    let is_v = note.x != null and x_scale
    let y_pos = if (is_h) float(scale.scale_apply(y_scale, note.y)) else 0.0
    let x_pos = if (is_v) float(scale.scale_apply(x_scale, note.x)) else 0.0
    let el = if (is_h and dash)
        <line x1: 0, y1: y_pos, x2: plot_w, y2: y_pos,
              stroke: color, 'stroke-width': stroke_w, 'stroke-dasharray': dash>
    else if (is_h)
        svg.line(0, y_pos, plot_w, y_pos, color, stroke_w)
    else if (is_v and dash)
        <line x1: x_pos, y1: 0, x2: x_pos, y2: plot_h,
              stroke: color, 'stroke-width': stroke_w, 'stroke-dasharray': dash>
    else if (is_v)
        svg.line(x_pos, 0, x_pos, plot_h, color, stroke_w)
    else null
    el
}

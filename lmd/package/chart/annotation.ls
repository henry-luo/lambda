// chart/annotation.ls — Annotation layer for the chart library
// Renders positioned annotations (text labels, reference lines) on charts.

import svg: .svg
import scale: .scale
import cfg: .config
import text: .text
import collision: .collision
import paint: .paint

// ============================================================
// Render all annotations from the annotation element
// ============================================================

pub fn render_annotations(annotation_el, x_scale, y_scale, plot_w, plot_h, theme) {
    let plan = prepare(annotation_el, x_scale, y_scale, plot_w, plot_h, theme);
    let selected = collision.select(plan.items |: ~.bounds != null) |> ~.index;
    render_plan({*:plan, visible: selected})
}

// Batch text metrics once; rendering and cross-guide selection share these exact positions.
pub fn prepare(annotation_el, x_scale, y_scale, plot_w, plot_h, theme, clipped = false) {
    let notes = if (annotation_el != null) content(annotation_el) else [];
    let texts = [for (index, note in notes where name(note) == 'text_note')
        {index: index, label: if (note.text != null) string(note.text) else "", font: note_font(note, theme)}];
    let metrics = text.measure_requests(texts);
    let items = if (metrics is error) [] else [for (index, note in notes) (
        let slot = index_of(texts |> ~.index, index),
        if (slot != null) (
            let label = texts[slot], let position = note_position(note, x_scale, y_scale),
            let anchor = if (note.anchor != null) note.anchor else "start",
            let bounds = text.bounds(metrics[slot], anchor, 0.0, position.x, position.y),
            {index: index, element: <text x: position.x, y: position.y, 'text-anchor': anchor,
                *:text.attributes(label.font), fill: paint.value(if (note.color != null) note.color else theme.title_color,
                    theme._paints), label.label>,
                bounds: if (label.label == "") null else if (clipped) clip_bounds(bounds, plot_w, plot_h) else bounds,
                optional: collision.enabled(note.label_overlap), gap: collision.gap(note.label_separation)})
        else {index: index, bounds: null, element: if (name(note) == 'rule_note')
            render_rule_note(note, x_scale, y_scale, plot_w, plot_h, theme._paints)
            else if (name(note) == 'region_note') render_region_note(note, x_scale, y_scale, plot_w, plot_h, theme._paints) else null})];
    {items: items, _error: if (metrics is error) metrics else null}
}

pub fn render_plan(plan) => if (plan._error is error) plan._error else
    svg.group_class("annotations", [for (item in plan.items where item.element != null and
        (item.bounds == null or plan.visible == null or item.index in plan.visible)) item.element])

fn clip_bounds(bounds, width, height) => if (bounds.right <= 0 or bounds.left >= width or bounds.bottom <= 0 or bounds.top >= height) null
    else {left: max([0.0, bounds.left]), right: min([width, bounds.right]),
        top: max([0.0, bounds.top]), bottom: min([height, bounds.bottom])}

fn note_font(note, theme) => text.style({font_family: if (note.font_family != null) note.font_family else theme.font,
    label_font_size: note.font_size, label_font_weight: note.font_weight, label_font_style: note.font_style,
    label_letter_spacing: note.letter_spacing, label_word_spacing: note.word_spacing})

// Missing bounds extend to the plot edge; min/abs also handle reversed scales.
fn render_region_note(note, x_scale, y_scale, plot_w, plot_h, paints) {
    let x1 = if (note.x != null and x_scale != null) scale.scale_apply(x_scale, note.x) else 0.0;
    let x2 = if (note.x2 != null and x_scale != null) scale.scale_apply(x_scale, note.x2) else plot_w;
    let y1 = if (note.y != null and y_scale != null) scale.scale_apply(y_scale, note.y) else 0.0;
    let y2 = if (note.y2 != null and y_scale != null) scale.scale_apply(y_scale, note.y2) else plot_h;
    <rect class: "annotation-region", x: min([x1, x2]), y: min([y1, y2]),
        width: abs(x2 - x1), height: abs(y2 - y1),
        fill: paint.value(if (note.color != null) note.color else "#edc948", paints),
        opacity: if (note.opacity != null) note.opacity else 0.2,
        *:cfg.settings({stroke: paint.value(note.stroke, paints), 'stroke-width': note.stroke_width}),
        if (note.text != null) <title note.text>>
}

// ============================================================
// Text annotation at a data position
// ============================================================

fn note_position(note, x_scale, y_scale) {
    let x = if (note.x != null and x_scale)
        float(scale.scale_apply(x_scale, note.x))
    else if (note.px != null) float(note.px)
    else 0.0
    let y = if (note.y != null and y_scale)
        float(scale.scale_apply(y_scale, note.y))
    else if (note.py != null) float(note.py)
    else 0.0
    let dy = if (note.dy) note.dy else 0
    let dx = if (note.dx) note.dx else 0;
    {x: float(x) + float(dx), y: float(y) + float(dy)}
}

// ============================================================
// Rule annotation (horizontal or vertical reference line)
// ============================================================

fn render_rule_note(note, x_scale, y_scale, plot_w, plot_h, paints) {
    let color = paint.value(if (note.color) note.color else "#888", paints)
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

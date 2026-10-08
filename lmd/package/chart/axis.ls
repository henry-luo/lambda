// chart/axis.ls — Axis generation for the chart library
// Generates SVG elements for x and y axes with ticks, labels, and titles.

import util: .util
import svg: .svg
import scale: .scale
import calendar: .calendar
import text: .text
import collision: .collision

// ============================================================
// Default axis configuration
// ============================================================

pub let default_axis_config = {
    tick_size: 5,
    tick_count: 8,
    label_font_size: 11,
    label_offset: 3,
    title_font_size: 13,
    title_padding: 10,
    domain_color: "#888",
    tick_color: "#888",
    grid_color: "#e0e0e0",
    label_color: "#333",
    title_color: "#333",
    enabled: true, domain: true, ticks: true, labels: true, label_angle: 0
}

// ============================================================
// Merge config helper: override defaults with provided config
// ============================================================

fn merge_config(config) {
    if config != null { {*:default_axis_config, *:config} }
    else { default_axis_config }
}

// ============================================================
// Temporal tick label formatting
// ============================================================

fn temporal_auto_format(lo_ms, hi_ms) {
    let span = hi_ms - lo_ms;
    let ms_day = 86400000.0;
    let ms_month = ms_day * 30.0;
    let ms_year = ms_day * 365.0;
    if (span > ms_year * 2.0) "YYYY"
    else if (span > ms_month * 2.0) "MMM YYYY"
    else if (span > ms_day * 2.0) "MMM DD"
    else if (span > 3600000.0 * 2.0) "hh:mm"
    else "hh:mm:ss"
}

fn format_tick_label(sc, tv, config = null) {
    if (sc.kind == "temporal" or config.dtype == "temporal")
        util.format_value(tv, if (config and config.format != null) config.format else temporal_auto_format(sc.domain[0], sc.domain[1]),
            "temporal", if (sc.timezone != null) sc.timezone else if (config.timezone != null) config.timezone else 0)
    else util.format_value(tv, config.format)
}

fn tick_values(sc, config) {
    let values = if (config.values != null) config.values else scale.scale_ticks(sc, config.tick_count);
    if (config.tick_min_step != null and len(values) > 1)
        [for (index, value in values where index == 0 or abs(float(value) - float(values[index - 1])) >= config.tick_min_step) value]
    else values
}

// keep formatted text and metrics together so layout and rendering use the same labels.
pub fn prepare(sc, config, title_text = null) {
    let options = merge_config(config);
    let active = sc != null and options.enabled != false and sc.kind != "identity";
    let title = if (not active or options.title_enabled == false) null else if (options.title != null) options.title else title_text;
    let values = if (active) tick_values(sc, options) else [];
    if (options._values == values and options._title == title) options
    else {
        let label_font = text.style(options, "label", 11);
        let title_font = text.style(options, "title", 13);
        let labels = if (options.labels == false) [] else text.fit(
            [for (value in values) format_tick_label(sc, value, options)], label_font, options.label_limit);
        let titles = if (title != null) text.measure([string(title)], title_font) else [];
        {*:options, _values: values, _title: title, _labels: if (labels is error) [] else labels,
            _title_metric: if (titles is array and len(titles) > 0) titles[0] else text.empty_metric,
            _label_font: label_font, _title_font: title_font,
            _error: util.first_error([labels, titles])}
    }
}

fn anchor(config, horizontal, far_side) => if (config.label_align == "left") "start"
    else if (config.label_align == "right") "end"
    else if (config.label_align == "center" or horizontal) "middle" else if (far_side) "start" else "end"

pub fn geometry(sc, pw, ph, config, horizontal) {
    let options = prepare(sc, config, config._title);
    let far_side = if (horizontal) options.orient == "top" else options.orient == "right";
    let direction = if (horizontal) (if (far_side) -1.0 else 1.0) else if (far_side) 1.0 else -1.0;
    let label_anchor = anchor(options, horizontal, far_side);
    let angle = if (options.label_angle != null) options.label_angle else 0.0;
    let tick_extent = if (options.ticks == false) 0.0 else options.tick_size;
    let rows = [for (index, value in options._values,
        let position = float(scale.scale_apply(sc, value)) + (if (sc.kind == "band") sc.bandwidth / 2.0 else 0.0),
        let label = options._labels[index],
        let local = text.bounds(label.metric, label_anchor, angle),
        let x = if (horizontal) position else if (far_side) tick_extent + options.label_offset - local.left
            else 0.0 - tick_extent - options.label_offset - local.right,
        let y = if (horizontal) (if (far_side) 0.0 - tick_extent - options.label_offset - local.bottom
            else tick_extent + options.label_offset - local.top) else position - (local.top + local.bottom) / 2.0,
        let bounds = text.bounds(label.metric, label_anchor, angle, x, y)
        where options.labels != false and position >= -1.0 and position <= (if (horizontal) pw else ph) + 1.0)
        {index: index, value: value, position: position, text: label.text, x: x, y: y, bounds: bounds}];
    let extent = max([float(tick_extent), for (row in rows)
        if (horizontal) (if (far_side) 0.0 - row.bounds.top else row.bounds.bottom)
        else if (far_side) row.bounds.right else 0.0 - row.bounds.left]);
    let title_angle = if (horizontal) 0 else if (far_side) 90 else -90;
    let title_bounds = text.bounds(options._title_metric, "middle", title_angle);
    let gap = extent + options.title_padding;
    let title_x = if (horizontal) pw / 2.0 else if (far_side) gap - title_bounds.left else 0.0 - gap - title_bounds.right;
    let title_y = if (not horizontal) ph / 2.0 else if (far_side) 0.0 - gap - title_bounds.bottom else gap - title_bounds.top;
    let total = if (options._title == null) extent else gap +
        (if (horizontal) title_bounds.bottom - title_bounds.top else title_bounds.right - title_bounds.left);
    {rows: rows, extent: total, title_x: title_x, title_y: title_y, title_angle: title_angle,
        title_bounds: if (options._title != null) text.bounds(options._title_metric, "middle", title_angle, title_x, title_y) else null,
        anchor: label_anchor, angle: angle, direction: direction, config: options}
}

fn interval(row, horizontal) => if (horizontal) {lo: row.bounds.left, hi: row.bounds.right}
    else {lo: row.bounds.top, hi: row.bounds.bottom}

fn select_rows(rows, horizontal, gap, index, previous, endpoint) {
    if (index >= len(rows) - 1) []
    else {
        let candidate = rows[index];
        let span = interval(candidate, horizontal);
        if (span.lo >= previous + gap and span.hi + gap <= endpoint)
            [candidate.index, *select_rows(rows, horizontal, gap, index + 1, span.hi, endpoint)]
        else select_rows(rows, horizontal, gap, index + 1, previous, endpoint)
    }
}

// cull against actual positions in either direction, retaining both endpoints when they fit.
fn visible_rows(rows, config, horizontal) {
    if (not collision.enabled(config.label_overlap) or len(rows) < 2)
        rows |> ~.index
    else {
        let ordered = sort(rows, (row) => row.position);
        let first = ordered[0];
        let endpoint = ordered[len(ordered) - 1];
        let gap = collision.gap(config.label_separation);
        let first_end = interval(first, horizontal).hi;
        let last_start = interval(endpoint, horizontal).lo;
        let keep_last = first_end + gap <= last_start;
        [first.index, *select_rows(ordered, horizontal, gap, 1, first_end, if (keep_last) last_start else inf),
            if (keep_last) endpoint.index]
    }
}

// Include side baselines and independent-axis offsets before comparing separate guides.
pub fn label_plan(sc, pw, ph, config, title_text, horizontal) {
    let options = prepare(sc, config, title_text);
    let geo = geometry(sc, pw, ph, options, horizontal);
    let far_side = if (horizontal) options.orient == "top" else options.orient == "right";
    let offset = if (options.offset != null) options.offset else 0.0;
    let dx = if (horizontal) 0.0 else (if (far_side) pw else 0.0) + offset;
    let dy = if (horizontal) (if (far_side) 0.0 else ph) + offset else 0.0;
    let active = options.enabled != false and sc != null and sc.kind != "identity";
    let visible = visible_rows(geo.rows, options, horizontal);
    {rows: if (active) [for (row in geo.rows where row.index in visible and row.text != "")
        {*:row, bounds: collision.translate(row.bounds, dx, dy), optional: collision.enabled(options.label_overlap),
            gap: collision.gap(options.label_separation)}] else [],
        title: if (active and geo.title_bounds != null) collision.translate(geo.title_bounds, dx, dy) else null,
        config: options}
}

fn render_axis(sc, pw, ph, config, title_text, horizontal) {
    let options = prepare(sc, config, title_text);
    if (options._error is error) options._error
    else if (options.enabled == false or sc.kind == "identity") null
    else {
        let geo = geometry(sc, pw, ph, options, horizontal);
        let far_side = if (horizontal) options.orient == "top" else options.orient == "right";
        let baseline = if (horizontal) (if (far_side) 0.0 else ph) else if (far_side) pw else 0.0;
        let domain_line = if (options.domain == false) null else if (horizontal)
            svg.line(0, baseline, pw, baseline, options.domain_color, 1)
            else svg.line(baseline, 0, baseline, ph, options.domain_color, 1);
        let visible = if (options._visible_rows != null) options._visible_rows else visible_rows(geo.rows, options, horizontal);
        let ticks = [for (index, value in options._values,
            let position = float(scale.scale_apply(sc, value)) + (if (sc.kind == "band") sc.bandwidth / 2.0 else 0.0),
            let row = geo.rows |: ~.index == index,
            let label = row[0]
            where position >= -1.0 and position <= (if (horizontal) pw else ph) + 1.0)
            <g class: "tick", transform: if (horizontal) svg.translate(position, baseline) else svg.translate(baseline, position),
                if (options.ticks != false) (if (horizontal)
                    svg.line(0, 0, 0, geo.direction * options.tick_size, options.tick_color, 1)
                    else svg.line(0, 0, geo.direction * options.tick_size, 0, options.tick_color, 1));
                if (options.labels != false and index in visible)
                    <text x: if (horizontal) 0.0 else label.x,
                        y: if (horizontal) label.y else label.y - position,
                        'text-anchor': geo.anchor, *:text.attributes(options._label_font), fill: options.label_color,
                        *:if (geo.angle != 0) {transform: svg.rotate(geo.angle,
                            if (horizontal) 0.0 else label.x, if (horizontal) label.y else label.y - position)} else {}, label.text>
            >];
        let title_x = geo.title_x + (if (horizontal) 0.0 else baseline);
        let title_y = geo.title_y + (if (horizontal) baseline else 0.0);
        let title = if (options._title != null) <text x: title_x, y: title_y,
            'text-anchor': "middle", *:text.attributes(options._title_font), fill: options.title_color,
            *:if (geo.title_angle != 0) {transform: svg.rotate(geo.title_angle, title_x, title_y)} else {}, options._title> else null;
        let result = svg.group_class(if (horizontal) "axis x-axis" else "axis y-axis",
            [for (item in [domain_line, *ticks, title] where item != null) item]);
        if (options.offset != null and options.offset != 0)
            svg.group(if (horizontal) svg.translate(0, options.offset) else svg.translate(options.offset, 0), [result])
        else result
    }
}

pub fn x_axis(sc, pw, ph, config, title_text) => render_axis(sc, pw, ph, config, title_text, true)
pub fn y_axis(sc, pw, ph, config, title_text) => render_axis(sc, pw, ph, config, title_text, false)

fn render_grid(sc, pw, ph, config, horizontal) {
    let cfg = merge_config(config);
    let values = tick_values(sc, cfg);
    let band_offset = if (sc.kind == "band") sc.bandwidth / 2.0 else 0.0;
    let lines = [for (value in values) (
        let position = float(scale.scale_apply(sc, value)) + band_offset,
        let extent = if (horizontal) pw else ph,
        if (position > 0.0 and position < extent)
            <line x1: if (horizontal) position else 0, y1: if (horizontal) 0 else position,
                x2: if (horizontal) position else pw, y2: if (horizontal) ph else position,
                stroke: cfg.grid_color, 'stroke-width': 1,
                'stroke-dasharray': if (cfg.grid_dash != null) cfg.grid_dash else "4,4">
        else null
    )] |: (~ != null);
    svg.group_class(if (horizontal) "grid x-grid" else "grid y-grid", lines)
}

pub fn x_axis_grid(sc, pw, ph, config) => render_grid(sc, pw, ph, config, true)
pub fn y_axis_grid(sc, pw, ph, config) => render_grid(sc, pw, ph, config, false)

// ============================================================
// Compute space needed for axes
// ============================================================

pub fn estimate_y_axis_width(sc, config, title_text = null) {
    let options = prepare(sc, config, title_text);
    geometry(sc, 0.0, max([0.0, *sc.range]), options, false).extent
}

pub fn estimate_x_axis_height(config, has_title: bool, sc = null, title_text = null) {
    let options = prepare(sc, config, if (has_title) title_text else null);
    geometry(sc, max([0.0, *sc.range]), 0.0, options, true).extent
}

// chart/layout.ls — Layout and positioning for the chart library
// Computes margins, plot area dimensions, and positions all chart components.

import axis: .axis
import leg: .legend
import parse: .parse
import cfg: .config
import text: .text
import util: .util
import collision: .collision
import paint: .paint

// ============================================================
// Compute layout from parsed spec
// ============================================================

// compute the layout geometry for a chart
// spec: parsed chart spec map
// returns: {plot_x, plot_y, plot_w, plot_h, legend_x, legend_y, total_w, total_h}
pub fn compute_layout(spec, x_scale, y_scale, has_color_legend: bool, color_categories, guides = null, axes = null) {
    let padding = spec.padding;
    let width = float(spec.width);
    let height = float(spec.height);

    // encoding info
    let enc = spec.encoding;
    let x_ch = parse.get_channel(enc, "x");
    let y_ch = parse.get_channel(enc, "y");
    let theme = cfg.resolve_theme(spec.config);
    let x_options = cfg.axis_config(theme, x_ch);
    let y_options = cfg.axis_config(theme, y_ch);
    let x_title = if (x_ch and x_ch.title) x_ch.title
        else if (x_ch and x_ch.field) x_ch.field
        else null;
    let y_title = if (y_ch and y_ch.title) y_ch.title
        else if (y_ch and y_ch.field) y_ch.field
        else null;

    let x_guides = axes |: ~.key == "x";
    let y_guides = axes |: ~.key == "y";
    let x_cfg = if (len(x_guides) > 0) x_guides[0].config else axis.prepare(x_scale, x_options, x_title);
    let y_cfg = if (len(y_guides) > 0) y_guides[0].config else axis.prepare(y_scale, y_options, y_title);
    let title = title_geometry(spec.title, theme);
    let x_geo = if (x_scale and x_cfg.enabled and x_scale.kind != "identity") axis.geometry(x_scale, width, height, x_cfg, true) else null;
    let y_geo = if (y_scale and y_cfg.enabled and y_scale.kind != "identity") axis.geometry(y_scale, width, height, y_cfg, false) else null;
    let x_rows = if (axes == null) x_geo.rows else [for (guide in x_guides)
        for (row in axis.geometry(guide.mapping, width, height, guide.config, true).rows) row];
    let y_rows = if (axes == null) y_geo.rows else [for (guide in y_guides)
        for (row in axis.geometry(guide.mapping, width, height, guide.config, false).rows) row];
    let y_axis_w = if (y_scale and y_cfg.enabled and y_scale.kind != "identity") axis.estimate_y_axis_width(y_scale, y_cfg, y_title) else 0.0;
    let x_axis_h = if (x_scale and x_cfg.enabled and x_scale.kind != "identity")
        axis.estimate_x_axis_height(x_cfg, x_title != null and x_cfg.title_enabled, x_scale, x_title) else 0.0;
    let legend_cfg = cfg.legend_config(theme, enc.color);
    let orient = if (legend_cfg.orient != null) legend_cfg.orient else "right";
    let legend_w = if (guides == null and has_color_legend and color_categories != null) leg.legend_width(color_categories, legend_cfg) + 20.0 else 0.0;
    let legend_h = if (guides == null and has_color_legend and color_categories != null)
        (if (enc.color.dtype == "quantitative") 150.0 else leg.legend_height(color_categories, legend_cfg, true)) + 20.0 else 0.0;
    let left_guides = guide_space(guides, "left", true, if (orient == "left") legend_w else 0.0);
    let right_guides = guide_space(guides, "right", true, if (orient == "right") legend_w else 0.0);
    let top_guides = guide_space(guides, "top", false, if (orient == "top") legend_h else 0.0);
    let bottom_guides = guide_space(guides, "bottom", false, if (orient == "bottom") legend_h else 0.0);
    let left_axes = max([axis_space(axes, "left", if (y_cfg.orient == "right") 0.0 else y_axis_w),
        for (row in x_rows) 0.0 - row.bounds.left]);
    let right_axes = max([axis_space(axes, "right", if (y_cfg.orient == "right") y_axis_w else 0.0),
        for (row in x_rows) row.bounds.right - width]);
    let top_axes = max([axis_space(axes, "top", if (x_cfg.orient == "top") x_axis_h else 0.0),
        for (row in y_rows) 0.0 - row.bounds.top]);
    let bottom_axes = max([axis_space(axes, "bottom", if (x_cfg.orient == "top") 0.0 else x_axis_h),
        for (row in y_rows) row.bounds.bottom - height]);
    let left_margin = float(padding.left) + left_axes + left_guides;
    let bottom_margin = float(padding.bottom) + bottom_axes + bottom_guides;
    let title_h = title.height;
    let top_margin = float(padding.top) + title_h + top_axes + top_guides;
    let right_margin = float(padding.right) + right_axes + right_guides;

    // plot area
    let plot_x = left_margin;
    let plot_y = top_margin;
    let natural_width = if (spec._plot_width != null) spec._plot_width + left_margin + right_margin else width;
    let natural_height = if (spec._plot_height != null) spec._plot_height + top_margin + bottom_margin else height;
    let total_width = if (spec._aspect_from == "width") natural_height * spec.aspect_ratio else natural_width;
    let total_height = if (spec._aspect_from == "height") natural_width / spec.aspect_ratio else natural_height;
    let plot_w_raw = total_width - left_margin - right_margin;
    let plot_h_raw = total_height - top_margin - bottom_margin;
    // Axis-free marks own their fit validation; do not enlarge their viewport.
    let min_plot = if ((x_scale or y_scale) and not spec._fit_container and
        spec._plot_width == null and spec._plot_height == null) 50.0 else 0.0;
    let plot_w = max([plot_w_raw, min_plot]);
    let plot_h = max([plot_h_raw, min_plot]);

    // legend position
    let positioned = [for (index, guide in guides) (
        let side = guide.orient,
        let offset = sum([for (prior in slice(guides, 0, index) where prior.orient == side) prior.height + 20.0]),
        {*:guide, x: if (side == "left") float(padding.left)
            else if (side == "top" or side == "bottom") plot_x else plot_x + plot_w + right_axes + 20.0,
            y: (if (side == "top") float(padding.top) + title_h
                else if (side == "bottom") plot_y + plot_h + bottom_axes + 20.0 else plot_y) + offset})];
    let legend_x = if (len(positioned) > 0) positioned[0].x else if (orient == "left") float(padding.left)
        else if (orient == "top" or orient == "bottom") plot_x else plot_x + plot_w + 20.0;
    let legend_y = if (len(positioned) > 0) positioned[0].y else if (orient == "top") float(padding.top) + title_h
        else if (orient == "bottom") plot_y + plot_h + x_axis_h + 20.0 else plot_y;

    {
        plot_x: plot_x,
        plot_y: plot_y,
        plot_w: plot_w,
        plot_h: plot_h,
        left_margin: left_margin,
        top_margin: top_margin,
        bottom_margin: bottom_margin,
        right_margin: right_margin,
        legend_x: legend_x,
        legend_y: legend_y,
        guides: positioned,
        title_y: float(padding.top) - title.metric.top,
        title_font: title.font,
        title_metric: title.metric,
        x_axis_config: x_cfg, y_axis_config: y_cfg,
        _error: util.first_error([x_cfg._error, y_cfg._error, title._error,
            if (spec._fit_container and (plot_w_raw < 0 or plot_h_raw < 0)) error("chart: container dimensions cannot fit guides") else null,
            for (guide in guides) guide._error, for (guide in axes) guide.config._error]),
        total_w: total_width,
        total_h: total_height
    }
}

// Fixed titles, legends, and authored notes protect their space before optional tick labels.
pub fn resolve_labels(axes, annotations, geometry) {
    let plans = [for (guide in axes) axis.label_plan(guide.mapping, geometry.plot_w, geometry.plot_h,
        guide.config, guide.title, guide.key == "x")];
    let obstacles = [for (plan in plans where plan.title != null) {bounds: plan.title},
        for (guide in geometry.guides) {bounds: {left: guide.x - geometry.plot_x,
            right: guide.x - geometry.plot_x + guide.width, top: guide.y - geometry.plot_y,
            bottom: guide.y - geometry.plot_y + guide.height}},
        if (geometry.title_metric.height > 0) {bounds: text.bounds(geometry.title_metric, "middle", 0.0,
            geometry.total_w / 2.0 - geometry.plot_x, geometry.title_y - geometry.plot_y)}];
    let candidates = [for (group, plan in annotations) for (item in plan.items where item.bounds != null)
        {*:item, kind: "note", group: group},
        for (group, plan in plans) for (row in plan.rows) {*:row, kind: "axis", group: group}];
    let selected = collision.select(candidates, obstacles);
    {axes: [for (group, guide in axes) {*:guide, config: {*:plans[group].config,
        _visible_rows: [for (item in selected where item.kind == "axis" and item.group == group) item.index]}}],
        annotations: [for (group, plan in annotations) {*:plan,
            visible: [for (item in selected where item.kind == "note" and item.group == group) item.index]}]}
}

pub fn title_geometry(title, theme) {
    let font = text.style({font_family: theme.font, *:theme}, "title", 16, 700);
    let metrics = if (title != null) text.measure([string(title)], font) else [];
    let metric = if (metrics is array and len(metrics) > 0) metrics[0] else text.empty_metric;
    {font: font, metric: metric, height: if (title != null) metric.height + 8.0 else 0.0,
        _error: if (metrics is error) metrics else null}
}

pub fn title_element(title, geometry, theme) => if (title != null)
    <text x: geometry.total_w / 2.0, y: geometry.title_y, 'text-anchor': "middle",
        *:text.attributes(geometry.title_font), fill: paint.value(theme.title_color, theme._paints), title> else null

fn guide_space(guides, orient, width, fallback) {
    if (guides == null) fallback
    else {
        let same_side = guides |: ~.orient == orient;
        if (len(same_side) == 0) 0.0
        else if (width) max(same_side |> ~.width) + 20.0
        else sum(same_side |> (~.height + 20.0))
    }
}

fn axis_space(axes, orient, fallback) => if (axes == null) fallback
    else sum([for (guide in axes where guide.orient == orient) guide.space])

// ============================================================
// Layout for arc (pie/donut) charts — centered
// ============================================================

pub fn compute_arc_layout(spec, has_color_legend: bool, color_categories, guides = null) {
    let geometry = compute_layout(spec, null, null, has_color_legend, color_categories, guides);
    {*:geometry, cx: geometry.plot_x + geometry.plot_w / 2.0,
        cy: geometry.plot_y + geometry.plot_h / 2.0,
        radius: min([geometry.plot_w, geometry.plot_h]) / 2.0}
}

// ============================================================
// Layout for faceted charts — grid of sub-charts
// ============================================================

pub fn compute_facet_layout(n_facets, columns, sub_w, sub_h, spacing, title, padding, theme = null, headers = null) {
    let cols = if (columns and columns > 0) columns else max([1, n_facets]);
    let rows = int(ceil(float(n_facets) / float(cols)));
    let sp = if (spacing != null) float(spacing) else 20.0;
    let pad = if (padding) padding else {top: 20, right: 20, bottom: 20, left: 20};
    let options = if (theme != null) theme else cfg.light_theme;
    let title_plan = title_geometry(title, options);
    let header_font = text.style({font_family: options.font}, "label", 12, 700);
    let metrics = text.measure(if (headers != null) headers else [], header_font);
    let header_h = max([0.0, for (metric in metrics) metric.height + 4.0]);
    let title_h = title_plan.height;

    // Empty grids have no inter-cell gaps in either direction.
    let occupied_cols = if (n_facets == 0) 0 else cols;
    let total_w = float(pad.left) + float(occupied_cols) * float(sub_w) + float(max([0, occupied_cols - 1])) * sp + float(pad.right);
    let total_h = float(pad.top) + title_h + float(rows) * (float(sub_h) + header_h) + float(max([0, rows - 1])) * sp + float(pad.bottom);

    {
        rows: rows,
        cols: cols,
        sub_w: float(sub_w),
        sub_h: float(sub_h),
        spacing: sp,
        header_h: header_h,
        headers: metrics, header_font: header_font,
        title_y: float(pad.top) - title_plan.metric.top, title_font: title_plan.font,
        _error: util.first_error([title_plan._error, metrics]),
        title_h: title_h,
        offset_x: float(pad.left),
        offset_y: float(pad.top) + title_h,
        total_w: total_w,
        total_h: total_h
    }
}

// position of the i-th facet cell (0-based)
pub fn facet_cell_pos(facet_layout, index) {
    let col = index % facet_layout.cols;
    let row = int(floor(float(index) / float(facet_layout.cols)));
    let x = facet_layout.offset_x + float(col) * (facet_layout.sub_w + facet_layout.spacing);
    let y = facet_layout.offset_y + float(row) * (facet_layout.sub_h + facet_layout.header_h + facet_layout.spacing);
    {x: x, y: y}
}

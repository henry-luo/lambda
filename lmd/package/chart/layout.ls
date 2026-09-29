// chart/layout.ls — Layout and positioning for the chart library
// Computes margins, plot area dimensions, and positions all chart components.

import axis: .axis
import leg: .legend
import parse: .parse

// ============================================================
// Compute layout from parsed spec
// ============================================================

// compute the layout geometry for a chart
// spec: parsed chart spec map
// returns: {plot_x, plot_y, plot_w, plot_h, legend_x, legend_y, total_w, total_h}
pub fn compute_layout(spec, x_scale, y_scale, has_color_legend: bool, color_categories) {
    let padding = spec.padding;
    let width = float(spec.width);
    let height = float(spec.height);

    // encoding info
    let enc = spec.encoding;
    let x_ch = parse.get_channel(enc, "x");
    let y_ch = parse.get_channel(enc, "y");
    let x_title = if (x_ch and x_ch.title) x_ch.title
        else if (x_ch and x_ch.field) x_ch.field
        else null;
    let y_title = if (y_ch and y_ch.title) y_ch.title
        else if (y_ch and y_ch.field) y_ch.field
        else null;

    // estimate axis sizes
    let left_margin = if (y_scale)
        axis.estimate_y_axis_width(y_scale, null) + float(padding.left)
    else float(padding.left);

    let bottom_margin = if (x_scale)
        axis.estimate_x_axis_height(null, x_title != null) + float(padding.bottom)
    else float(padding.bottom);

    let top_margin_base = float(padding.top);
    let title_h = if (spec.title) 24.0 else 0.0;
    let top_margin = top_margin_base + title_h;

    // legend space
    let legend_w = if (has_color_legend and color_categories)
        leg.legend_width(color_categories, null) + 20.0
    else 0.0;
    let right_margin = float(padding.right) + legend_w;

    // plot area
    let plot_x = left_margin;
    let plot_y = top_margin;
    let plot_w_raw = width - left_margin - right_margin;
    let plot_h_raw = height - top_margin - bottom_margin;
    let plot_w = if (plot_w_raw < 50.0) 50.0 else plot_w_raw;
    let plot_h = if (plot_h_raw < 50.0) 50.0 else plot_h_raw;

    // legend position
    let legend_x = plot_x + plot_w + 20.0;
    let legend_y = plot_y;

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
        title_y: if (spec.title) float(padding.top) + 16.0 else 0.0,
        total_w: width,
        total_h: height
    }
}

// ============================================================
// Layout for arc (pie/donut) charts — centered
// ============================================================

pub fn compute_arc_layout(spec, has_color_legend: bool, color_categories) {
    let padding = spec.padding;
    let width = float(spec.width);
    let height = float(spec.height);
    let title_h = if (spec.title) 24.0 else 0.0;

    // legend space
    let legend_w = if (has_color_legend and color_categories)
        leg.legend_width(color_categories, null) + 20.0
    else 0.0;

    let avail_w = width - float(padding.left) - float(padding.right) - legend_w;
    let avail_h = height - float(padding.top) - float(padding.bottom) - title_h;
    let radius = min([avail_w, avail_h]) / 2.0;

    let cx = float(padding.left) + avail_w / 2.0;
    let cy = float(padding.top) + title_h + avail_h / 2.0;

    let legend_x = float(padding.left) + avail_w + 20.0;
    let legend_y = float(padding.top) + title_h;

    {
        cx: cx,
        cy: cy,
        radius: radius,
        title_y: if (spec.title) float(padding.top) + 16.0 else 0.0,
        legend_x: legend_x,
        legend_y: legend_y,
        total_w: width,
        total_h: height
    }
}

// ============================================================
// Layout for faceted charts — grid of sub-charts
// ============================================================

pub fn compute_facet_layout(n_facets, columns, sub_w, sub_h, spacing, title, padding) {
    let cols = if (columns and columns > 0) columns else n_facets;
    let rows = int(ceil(float(n_facets) / float(cols)));
    let sp = if (spacing) float(spacing) else 20.0;
    let pad = if (padding) padding else {top: 20, right: 20, bottom: 20, left: 20};
    let header_h = 18.0;
    let title_h = if (title) 24.0 else 0.0;

    let total_w = float(pad.left) + float(cols) * float(sub_w) + float(cols - 1) * sp + float(pad.right);
    let total_h = float(pad.top) + title_h + float(rows) * (float(sub_h) + header_h) + float(rows - 1) * sp + float(pad.bottom);

    {
        rows: rows,
        cols: cols,
        sub_w: float(sub_w),
        sub_h: float(sub_h),
        spacing: sp,
        header_h: header_h,
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

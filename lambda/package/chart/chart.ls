// chart/chart.ls — Main entry point for the Lambda Chart Library
// Provides chart.render(spec) -> SVG element tree

import parse: .lambda.package.chart.parse
import transform: .lambda.package.chart.transform
import scale: .lambda.package.chart.scale
import mark: .lambda.package.chart.mark
import axis: .lambda.package.chart.axis
import leg: .lambda.package.chart.legend
import layout: .lambda.package.chart.layout
import svg: .lambda.package.chart.svg
import color: .lambda.package.chart.color
import util: .lambda.package.chart.util

// ============================================================
// Public API: render a <chart> element into an SVG element
// ============================================================

pub fn render(chart_el) {
    let spec = parse.parse_chart(chart_el);

    // handle layer composition
    if (spec.layer) render_layered(spec)
    // handle arc/pie chart (no x/y axes)
    else if (spec.mark and spec.mark.kind == "arc") render_arc(spec)
    // standard single-view chart
    else render_single(spec)
}

// render from a pre-parsed spec map (no element tree needed)
pub fn render_spec(spec) {
    if (spec.layer) render_layered(spec)
    else if (spec.mark and spec.mark.kind == "arc") render_arc(spec)
    else render_single(spec)
}

// ============================================================
// Single-view chart rendering
// ============================================================

fn render_single(spec) {
    // resolve data
    let raw_data = if (spec.data) spec.data else [];

    // apply transforms
    let data = transform.apply_transforms(raw_data, spec.transform);

    let enc = spec.encoding;
    let mark_spec = spec.mark;
    let mark_type = if (mark_spec) mark_spec.kind else "point";

    // extract encoding channels
    let x_ch = parse.get_channel(enc, "x");
    let y_ch = parse.get_channel(enc, "y");
    let color_ch = parse.get_channel(enc, "color");
    let size_ch = parse.get_channel(enc, "size");
    let opacity_ch = parse.get_channel(enc, "opacity");
    let text_ch = parse.get_channel(enc, "text");

    let x_field = if (x_ch) x_ch.field else null;
    let y_field = if (y_ch) y_ch.field else null;
    let color_field = if (color_ch) color_ch.field else null;

    // determine if categorical/quantitative for position scales
    let x_type = if (x_ch) x_ch.dtype else "nominal";
    let y_type = if (y_ch) y_ch.dtype else "quantitative";

    // build color scale
    let color_scale = if (color_ch) scale.infer_color_scale(color_ch, data) else null;
    let color_categories = if (color_ch and (color_ch.dtype == "nominal" or color_ch.dtype == "ordinal"))
        util.unique_vals(data | ~[color_field])
    else null;
    let has_legend = color_categories != null and len(color_categories) > 0;

    // compute layout (need rough scales first for axis size estimation)
    let temp_x_scale = build_position_scale(x_ch, data, 0.0, float(spec.width), mark_type, true);
    let temp_y_scale = build_position_scale(y_ch, data, float(spec.height), 0.0, mark_type, false);
    let lay = layout.compute_layout(spec, temp_x_scale, temp_y_scale, has_legend, color_categories);

    // rebuild scales with actual plot dimensions
    let x_scale = build_position_scale(x_ch, data, 0.0, lay.plot_w, mark_type, true);
    let y_scale = build_position_scale(y_ch, data, lay.plot_h, 0.0, mark_type, false);

    // build size scale if needed
    let size_scale = if (size_ch and size_ch.field)
        (let size_values = data | float(~[size_ch.field]),
         scale.linear_scale_nice(size_values, 20.0, 200.0, false))
    else null;

    // build opacity scale if needed
    let opacity_scale = if (opacity_ch and opacity_ch.field)
        (let op_values = data | float(~[opacity_ch.field]),
         scale.linear_scale_nice(op_values, 0.2, 1.0, false))
    else null;

    // render marks
    let mark_ctx = {
        x_scale: x_scale, y_scale: y_scale,
        plot_w: lay.plot_w, plot_h: lay.plot_h,
        color_scale: color_scale, color_field: color_field,
        size_scale: size_scale,
        size_field: if (size_ch) size_ch.field else null,
        opacity_scale: opacity_scale,
        opacity_field: if (opacity_ch) opacity_ch.field else null,
        x_field: x_field, y_field: y_field,
        text_field: if (text_ch) text_ch.field else null
    };
    let marks_el = render_mark(mark_type, data, mark_ctx, mark_spec);

    // render axes
    let x_title = if (x_ch and x_ch.title) x_ch.title
        else if (x_field) x_field else null;
    let y_title = if (y_ch and y_ch.title) y_ch.title
        else if (y_field) y_field else null;

    let x_axis_el = if (x_scale) axis.x_axis(x_scale, lay.plot_w, lay.plot_h, null, x_title) else null;
    let y_axis_el = if (y_scale) axis.y_axis(y_scale, lay.plot_w, lay.plot_h, null, y_title) else null;

    // render grid if config requests it
    let grid_config = spec.config;
    let show_grid = if (grid_config and grid_config.axis_grid) grid_config.axis_grid else false;
    let y_grid_el = if (show_grid and y_scale)
        axis.y_axis_grid(y_scale, lay.plot_w, lay.plot_h, null)
    else null;

    // render legend
    let legend_el = if (has_legend)
        (let legend_title = if (color_ch and color_ch.title) color_ch.title
            else if (color_field) color_field else null,
         leg.color_legend(color_categories, color_scale, legend_title, null))
    else null;

    // assemble SVG
    assemble_svg(spec, lay, marks_el, x_axis_el, y_axis_el, y_grid_el, legend_el)
}

// ============================================================
// Layered chart rendering
// ============================================================

fn render_layered(spec) {
    let layers = spec.layer;
    let data = if (spec.data) spec.data else [];

    // each layer inherits parent data if not overridden
    let layer_specs = (for (layer in layers) {
        data: if (layer.data) layer.data else data,
        mark: layer.mark,
        encoding: layer.encoding
    });

    // build unified scales across all layers
    let enc = if (len(layer_specs) > 0) layer_specs[0].encoding else {};
    let all_data = data;

    let x_ch = parse.get_channel(enc, "x");
    let y_ch = parse.get_channel(enc, "y");
    let color_ch = parse.get_channel(enc, "color");
    let x_field = if (x_ch) x_ch.field else null;
    let y_field = if (y_ch) y_ch.field else null;
    let color_field = if (color_ch) color_ch.field else null;
    let mark_type = if (layer_specs[0].mark) layer_specs[0].mark.kind else "line";

    let color_scale = if (color_ch) scale.infer_color_scale(color_ch, all_data) else null;
    let color_categories = if (color_ch and (color_ch.dtype == "nominal" or color_ch.dtype == "ordinal"))
        util.unique_vals(all_data | ~[color_field])
    else null;
    let has_legend = color_categories != null and len(color_categories) > 0;

    let temp_x_scale = build_position_scale(x_ch, all_data, 0.0, float(spec.width), mark_type, true);
    let temp_y_scale = build_position_scale(y_ch, all_data, float(spec.height), 0.0, mark_type, false);
    let lay = layout.compute_layout(spec, temp_x_scale, temp_y_scale, has_legend, color_categories);

    let x_scale = build_position_scale(x_ch, all_data, 0.0, lay.plot_w, mark_type, true);
    let y_scale = build_position_scale(y_ch, all_data, lay.plot_h, 0.0, mark_type, false);

    // render each layer's marks
    let layer_marks = (for (ls in layer_specs)
        (let l_enc = ls.encoding,
         let l_mark = ls.mark,
         let l_mark_type = if (l_mark) l_mark.kind else "line",
         let l_color_ch = parse.get_channel(l_enc, "color"),
         let l_color_field = if (l_color_ch) l_color_ch.field else color_field,
         let l_size_ch = parse.get_channel(l_enc, "size"),
         let l_x_ch = parse.get_channel(l_enc, "x"),
         let l_y_ch = parse.get_channel(l_enc, "y"),
         let l_text_ch = parse.get_channel(l_enc, "text"),
         let lx = if (l_x_ch) l_x_ch.field else x_field,
         let ly = if (l_y_ch) l_y_ch.field else y_field,
         let l_ctx = {
             x_scale: x_scale, y_scale: y_scale,
             plot_w: lay.plot_w, plot_h: lay.plot_h,
             color_scale: color_scale, color_field: l_color_field,
             size_scale: null, size_field: null,
             x_field: lx, y_field: ly,
             text_field: if (l_text_ch) l_text_ch.field else null
         },
         render_mark(l_mark_type, ls.data, l_ctx, l_mark))
    );

    // axes
    let x_title = if (x_ch and x_ch.title) x_ch.title
        else if (x_field) x_field else null;
    let y_title = if (y_ch and y_ch.title) y_ch.title
        else if (y_field) y_field else null;

    let x_axis_el = if (x_scale) axis.x_axis(x_scale, lay.plot_w, lay.plot_h, null, x_title) else null;
    let y_axis_el = if (y_scale) axis.y_axis(y_scale, lay.plot_w, lay.plot_h, null, y_title) else null;

    // legend
    let legend_el = if (has_legend)
        (let legend_title = if (color_ch and color_ch.title) color_ch.title
            else if (color_field) color_field else null,
         leg.color_legend(color_categories, color_scale, legend_title, null))
    else null;

    // assemble with all layer marks
    let all_marks = svg.group_class("marks layers", layer_marks);
    assemble_svg(spec, lay, all_marks, x_axis_el, y_axis_el, null, legend_el)
}

// ============================================================
// Arc (pie/donut) chart rendering
// ============================================================

fn render_arc(spec) {
    let raw_data = if (spec.data) spec.data else [];
    let data = transform.apply_transforms(raw_data, spec.transform);

    let enc = spec.encoding;
    let mark_spec = spec.mark;
    let theta_ch = parse.get_channel(enc, "theta");
    let color_ch = parse.get_channel(enc, "color");
    let theta_field = if (theta_ch) theta_ch.field else null;
    let color_field = if (color_ch) color_ch.field else null;

    // color scale
    let color_scale = if (color_ch) scale.infer_color_scale(color_ch, data) else null;
    let color_categories = if (color_ch)
        util.unique_vals(data | ~[color_field])
    else null;
    let has_legend = color_categories != null and len(color_categories) > 0;

    // layout
    let lay = layout.compute_arc_layout(spec, has_legend, color_categories);
    let inner_r = if (mark_spec.inner_radius) float(mark_spec.inner_radius) else 0.0;
    let outer_r = if (mark_spec.outer_radius) float(mark_spec.outer_radius) else lay.radius;

    // render arcs
    let arc_ctx = {
        theta_field: theta_field,
        color_scale: color_scale, color_field: color_field,
        cx: lay.cx, cy: lay.cy,
        inner_radius: inner_r, outer_radius: outer_r
    };
    let arcs_el = mark.arc_mark(data, arc_ctx, mark_spec);

    // legend
    let legend_el = if (has_legend)
        (let legend_title = if (color_ch and color_ch.title) color_ch.title
            else if (color_field) color_field else null,
         leg.color_legend(color_categories, color_scale, legend_title, null))
    else null;

    // assemble
    let width = int(lay.total_w);
    let height = int(lay.total_h);
    let bg = <rect width: width, height: height, fill: "white">;

    let children0 = [bg, arcs_el];

    // title
    let children1 = if (spec.title)
        [*children0,
         <text x: lay.total_w / 2.0, y: lay.title_y,
               "text-anchor": "middle", "font-size": 16, "font-weight": "bold", fill: "#333";
             spec.title
         >]
    else children0;

    // legend
    let children = if (legend_el)
        [*children1,
         <g transform: svg.translate(lay.legend_x, lay.legend_y); legend_el>]
    else children1;

    svg.svg_root(width, height, children)
}

// ============================================================
// Build a position scale (x or y)
// ============================================================

fn build_position_scale(channel, data, rlo, rhi, mark_type: string, is_x: bool) {
    if not channel {
        null
    } else {
        let field_name = channel.field
        let data_type = channel.dtype
        if not field_name {
            null
        } else {
            let values = data | ~[field_name]
            if data_type == "quantitative" {
                let include_zero = if (mark_type == "bar") true
                    else if (channel.zero) channel.zero
                    else false
                scale.linear_scale_nice(values, rlo, rhi, include_zero)
            } else if data_type == "temporal" {
                let num_vals = values | float(~)
                scale.linear_scale_nice(num_vals, rlo, rhi, false)
            } else {
                // nominal or ordinal
                let cats = util.unique_vals(values)
                if (mark_type == "bar" and is_x)
                    scale.band_scale(cats, rlo, rhi, 4.0)
                else if (mark_type == "bar" and not is_x)
                    scale.band_scale(cats, rlo, rhi, 4.0)
                else
                    scale.point_scale(cats, rlo, rhi, 20.0)
            }
        }
    }
}

// ============================================================
// Dispatch mark rendering
// ============================================================

fn render_mark(mark_type, data, ctx, mark_spec) {
    if (mark_type == "bar")
        mark.bar(data, ctx, mark_spec)
    else if (mark_type == "line")
        mark.line_mark(data, ctx, mark_spec)
    else if (mark_type == "area")
        mark.area_mark(data, ctx, mark_spec)
    else if (mark_type == "point")
        mark.point_mark(data, ctx, mark_spec)
    else if (mark_type == "text")
        mark.text_mark(data, ctx, mark_spec)
    else if (mark_type == "rule")
        mark.rule_mark(data, ctx, mark_spec)
    else if (mark_type == "tick")
        mark.tick_mark(data, ctx, mark_spec)
    else
        // default: point
        mark.point_mark(data, ctx, mark_spec)
}

// ============================================================
// Assemble final SVG from components
// ============================================================

fn assemble_svg(spec, lay, marks_el, x_axis_el, y_axis_el, grid_el, legend_el) {
    let width = int(lay.total_w);
    let height = int(lay.total_h);

    // background
    let bg = <rect width: width, height: height, fill: "white">;

    // plot group contents
    let plot_children0 = [marks_el];
    let plot_children1 = if (grid_el) [grid_el, *plot_children0] else plot_children0;
    let plot_children2 = if (x_axis_el) [*plot_children1, x_axis_el] else plot_children1;
    let plot_children = if (y_axis_el) [*plot_children2, y_axis_el] else plot_children2;

    // plot group (translated by margins)
    let plot_group = svg.group(svg.translate(lay.plot_x, lay.plot_y), plot_children);

    let children0 = [bg, plot_group];

    // title
    let children1 = if (spec.title)
        [*children0,
         <text x: lay.total_w / 2.0, y: lay.title_y,
               "text-anchor": "middle", "font-size": 16, "font-weight": "bold", fill: "#333";
             spec.title
         >]
    else children0;

    // legend
    let children = if (legend_el)
        [*children1,
         <g transform: svg.translate(lay.legend_x, lay.legend_y); legend_el>]
    else children1;

    svg.svg_root(width, height, children)
}

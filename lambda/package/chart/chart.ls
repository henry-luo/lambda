// chart/chart.ls — Main entry point for the Lambda Chart Library
// Provides chart.render(spec) -> SVG element tree

import parse: .parse
import transform: .transform
import scale: .scale
import mark: .mark
import axis: .axis
import leg: .legend
import layout: .layout
import svg: .svg
import color: .color
import util: .util
import stack: .stack
import cfg: .config
import ann: .annotation

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
    let s = if (spec is element) parse.parse_chart(spec) else spec;
    if (s.layer) render_layered(s)
    else if (s.mark and s.mark.kind == "arc") render_arc(s)
    else render_single(s)
}

// ============================================================
// Single-view chart rendering
// ============================================================

fn render_single(spec) {
    // resolve theme
    let theme = cfg.resolve_theme(spec.config);
    let axis_cfg = cfg.axis_config(theme);
    let legend_cfg = cfg.legend_config(theme);

    // resolve data
    let raw_data = if (spec.data) spec.data else [];

    // apply transforms
    let data_transformed0 = transform.apply_transforms(raw_data, spec.transform);

    let enc = spec.encoding;
    let mark_spec = spec.mark;
    let mark_type = if (mark_spec) mark_spec.kind else "point";

    // extract encoding channels
    let x_ch0 = parse.get_channel(enc, "x");
    let y_ch0 = parse.get_channel(enc, "y");

    // histogram auto-transform: if x has bin:true and y has aggregate:"count"
    let hist = apply_histogram_transform(data_transformed0, x_ch0, y_ch0);
    let data_transformed = hist.data;
    let x_ch = hist.x_ch;
    let y_ch = hist.y_ch;
    let color_ch = parse.get_channel(enc, "color");
    let size_ch = parse.get_channel(enc, "size");
    let opacity_ch = parse.get_channel(enc, "opacity");
    let text_ch = parse.get_channel(enc, "text");
    let x_offset_ch = parse.get_channel(enc, "x_offset");
    let y2_ch = parse.get_channel(enc, "y2");
    let detail_ch = parse.get_channel(enc, "detail");
    let tooltip_ch = parse.get_channel(enc, "tooltip");

    let x_field = if (x_ch) x_ch.field else null;
    let y_field = if (y_ch) y_ch.field else null;
    let color_field0 = if (color_ch) color_ch.field else null;
    let x_offset_field = if (x_offset_ch) x_offset_ch.field else null;
    let y2_field = if (y2_ch) y2_ch.field else null;
    let detail_field = if (detail_ch) detail_ch.field else null;
    let tooltip_field = if (tooltip_ch) tooltip_ch.field else null;

    // determine if categorical/quantitative for position scales
    let x_type = if (x_ch) x_ch.dtype else "nominal";
    let y_type = if (y_ch) y_ch.dtype else "quantitative";

    // detect and apply stacking
    let stack_mode = detect_stack_mode(mark_type, y_ch, color_field0, x_offset_field);
    let data_stacked = if (stack_mode and x_field and y_field and color_field0)
        stack.apply_stack(data_transformed, y_field, color_field0, x_field, stack_mode)
    else data_transformed;

    // conditional color encoding: add _cond_color field
    let has_condition = color_ch and color_ch.condition and not color_ch.field;
    let data = if (has_condition)
        (let cond = color_ch.condition,
         let default_val = if (color_ch.value) color_ch.value else color.default_color,
         let cond_field = cond.field,
         for (d in data_stacked) (
             let fv = d[cond_field],
             let cv = if (cond.equal != null) (if (fv == cond.equal) cond.value else default_val)
                 else if (cond.gt != null) (if (float(fv) > float(cond.gt)) cond.value else default_val)
                 else if (cond.lt != null) (if (float(fv) < float(cond.lt)) cond.value else default_val)
                 else default_val,
             let pairs = for (k, val in d) for (x in [string(k), val]) x,
             map([*pairs, "_cond_color", cv])
         ))
    else data_stacked;

    let color_field = if (has_condition) "_cond_color" else color_field0;

    // x_offset categories for grouped bars
    let x_offset_cats = if (x_offset_field)
        util.unique_vals(data | ~[x_offset_field])
    else null;

    // build color scale (skip for conditional colors)
    let color_scale = if (has_condition) null
        else if (color_ch and color_ch.field) scale.infer_color_scale(color_ch, data)
        else null;
    let color_categories = if (not has_condition and color_ch and (color_ch.dtype == "nominal" or color_ch.dtype == "ordinal"))
        util.unique_vals(data | ~[color_field])
    else null;
    let has_legend = color_categories != null and len(color_categories) > 0;

    // compute layout (need rough scales first for axis size estimation)
    let temp_x_scale = build_position_scale(x_ch, data, 0.0, float(spec.width), mark_type, true);
    let temp_y_scale = if (stack_mode)
        build_stacked_y_scale(data, float(spec.height), 0.0, stack_mode)
    else build_position_scale_y2(y_ch, data, float(spec.height), 0.0, mark_type, y2_field);
    let lay = layout.compute_layout(spec, temp_x_scale, temp_y_scale, has_legend, color_categories);

    // rebuild scales with actual plot dimensions
    let x_scale = build_position_scale(x_ch, data, 0.0, lay.plot_w, mark_type, true);
    let y_scale = if (stack_mode)
        build_stacked_y_scale(data, lay.plot_h, 0.0, stack_mode)
    else build_position_scale_y2(y_ch, data, lay.plot_h, 0.0, mark_type, y2_field);

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
        y2_field: y2_field,
        text_field: if (text_ch) text_ch.field else null,
        is_stacked: stack_mode != null,
        x_offset_field: x_offset_field,
        x_offset_cats: x_offset_cats,
        tooltip_field: tooltip_field,
        detail_field: detail_field
    };
    let marks_el = render_mark(mark_type, data, mark_ctx, mark_spec);

    // render annotations
    let annotation_el = if (spec.annotation)
        ann.render_annotations(spec.annotation, x_scale, y_scale, lay.plot_w, lay.plot_h, theme)
    else null;
    let marks_with_ann = if (annotation_el)
        svg.group_class("plot-content", [marks_el, annotation_el])
    else marks_el;

    // render axes
    let x_title = if (x_ch and x_ch.title) x_ch.title
        else if (x_field) x_field else null;
    let y_title = if (y_ch and y_ch.title) y_ch.title
        else if (y_field) y_field else null;

    let x_axis_el = if (x_scale) axis.x_axis(x_scale, lay.plot_w, lay.plot_h, axis_cfg, x_title) else null;
    let y_axis_el = if (y_scale) axis.y_axis(y_scale, lay.plot_w, lay.plot_h, axis_cfg, y_title) else null;

    // render grid if config requests it
    let grid_config = spec.config;
    let show_grid = if (grid_config and grid_config.axis_grid) grid_config.axis_grid else false;
    let y_grid_el = if (show_grid and y_scale)
        axis.y_axis_grid(y_scale, lay.plot_w, lay.plot_h, axis_cfg)
    else null;

    // render legend
    let legend_el = if (has_legend)
        (let legend_title = if (color_ch and color_ch.title) color_ch.title
            else if (color_field) color_field else null,
         leg.color_legend(color_categories, color_scale, legend_title, legend_cfg))
    else null;

    // assemble SVG
    assemble_svg(spec, lay, marks_with_ann, x_axis_el, y_axis_el, y_grid_el, legend_el, theme)
}

// ============================================================
// Layered chart rendering
// ============================================================

fn render_layered(spec) {
    let theme = cfg.resolve_theme(spec.config);
    let axis_cfg = cfg.axis_config(theme);
    let legend_cfg = cfg.legend_config(theme);
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

    let x_axis_el = if (x_scale) axis.x_axis(x_scale, lay.plot_w, lay.plot_h, axis_cfg, x_title) else null;
    let y_axis_el = if (y_scale) axis.y_axis(y_scale, lay.plot_w, lay.plot_h, axis_cfg, y_title) else null;

    // legend
    let legend_el = if (has_legend)
        (let legend_title = if (color_ch and color_ch.title) color_ch.title
            else if (color_field) color_field else null,
         leg.color_legend(color_categories, color_scale, legend_title, legend_cfg))
    else null;

    // assemble with all layer marks
    let all_marks = svg.group_class("marks layers", layer_marks);
    assemble_svg(spec, lay, all_marks, x_axis_el, y_axis_el, null, legend_el, theme)
}

// ============================================================
// Arc (pie/donut) chart rendering
// ============================================================

fn render_arc(spec) {
    let theme = cfg.resolve_theme(spec.config);
    let legend_cfg = cfg.legend_config(theme);
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
         leg.color_legend(color_categories, color_scale, legend_title, legend_cfg))
    else null;

    // assemble
    let width = int(lay.total_w);
    let height = int(lay.total_h);
    let bg = <rect width: width, height: height, fill: theme.background>;

    let children0 = [bg, arcs_el];

    // title
    let children1 = if (spec.title)
        [*children0,
         <text x: lay.total_w / 2.0, y: lay.title_y,
               'text-anchor': "middle", 'font-size': theme.title_font_size, 'font-weight': "bold", fill: theme.title_color;
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
                scale.temporal_scale(values, rlo, rhi)
            } else {
                // nominal or ordinal
                let cats = util.unique_vals(values)
                if ((mark_type == "bar" or mark_type == "rect" or mark_type == "boxplot") and is_x)
                    scale.band_scale(cats, rlo, rhi, 4.0)
                else if ((mark_type == "bar" or mark_type == "rect") and not is_x)
                    scale.band_scale(cats, rlo, rhi, 4.0)
                else
                    scale.point_scale(cats, rlo, rhi, 20.0)
            }
        }
    }
}

// ============================================================
// Histogram auto-transform (bin + count)
// ============================================================

fn apply_histogram_transform(data, x_ch, y_ch) {
    let has_bin = x_ch and x_ch.bin == true;
    let has_count = y_ch and y_ch.aggregate == "count";
    if (not has_bin or not has_count) {
        {data: data, x_ch: x_ch, y_ch: y_ch}
    } else {
        let field = x_ch.field;
        let bin_field = field ++ "_bin";
        let bin_end = bin_field ++ "_end";
        let maxbins = if (x_ch.bin != true and x_ch.bin.maxbins) x_ch.bin.maxbins else 10;
        // apply binning
        let values = data | float(~[field]);
        let vmin = min(values);
        let vmax = max(values);
        let range_span = vmax - vmin;
        let step = util.nice_num(range_span / float(maxbins), true);
        let binned = for (d in data) (
            let v = float(d[field]),
            let bs = floor(v / step) * step,
            let be = bs + step,
            // add both fields in one map() call to avoid chained add_field issue
            let pairs = for (k, val in d) for (x in [k, val]) x,
            map([*pairs, bin_field, bs, bin_end, be])
        );
        // aggregate: count per bin
        let bin_keys = util.unique_vals(binned | string(~[bin_field]));
        let counted = for (bk in bin_keys) (
            let items = binned that string(~[bin_field]) == bk,
            map([bin_field, items[0][bin_field], bin_end, items[0][bin_end], "_count", len(items)])
        );
        // override channels: x→nominal on bin_field, y→quantitative on _count
        let new_x = {*: x_ch, field: bin_field, dtype: "ordinal", bin: null};
        let new_y = {*: y_ch, field: "_count", dtype: "quantitative", aggregate: null};
        {data: counted, x_ch: new_x, y_ch: new_y}
    }
}

// ============================================================
// Stacking helpers
// ============================================================

fn detect_stack_mode(mark_type, y_ch, color_field, x_offset_field) {
    let s = if (y_ch) y_ch.stack else null
    if (s == "zero" or s == "normalize" or s == "center") s
    else if (s == false or s == "none") null
    else if ((mark_type == "bar" or mark_type == "area") and color_field and not x_offset_field) "zero"
    else null
}

fn build_stacked_y_scale(data, rlo, rhi, mode) {
    let y0_vals = data | float(~["_y0"])
    let y1_vals = data | float(~["_y1"])
    let all_vals = [*y0_vals, *y1_vals]
    let include_zero = mode != "center"
    scale.linear_scale_nice(all_vals, rlo, rhi, include_zero)
}

// build y scale that also considers y2_field for dual-value marks
fn build_position_scale_y2(y_ch, data, rlo, rhi, mark_type, y2_field) {
    if (not y2_field) build_position_scale(y_ch, data, rlo, rhi, mark_type, false)
    else if (not y_ch) null
    else {
        let field_name = y_ch.field
        if not field_name { null }
        else {
            let y_vals = data | float(~[field_name])
            let y2_vals = data | float(~[y2_field])
            let all_vals = [*y_vals, *y2_vals]
            let include_zero = if (mark_type == "bar") true
                else if (y_ch.zero != null) y_ch.zero
                else false
            scale.linear_scale_nice(all_vals, rlo, rhi, include_zero)
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
    else if (mark_type == "boxplot")
        mark.boxplot_mark(data, ctx, mark_spec)
    else if (mark_type == "errorbar")
        mark.errorbar_mark(data, ctx, mark_spec)
    else if (mark_type == "errorband")
        mark.errorband_mark(data, ctx, mark_spec)
    else if (mark_type == "rect")
        mark.rect_mark(data, ctx, mark_spec)
    else
        // default: point
        mark.point_mark(data, ctx, mark_spec)
}

// ============================================================
// Assemble final SVG from components
// ============================================================

fn assemble_svg(spec, lay, marks_el, x_axis_el, y_axis_el, grid_el, legend_el, theme) {
    let width = int(lay.total_w);
    let height = int(lay.total_h);

    // background
    let bg = <rect width: width, height: height, fill: theme.background>;

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
               'text-anchor': "middle", 'font-size': theme.title_font_size, 'font-weight': "bold", fill: theme.title_color;
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

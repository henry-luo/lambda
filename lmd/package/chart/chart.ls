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
import cloud: .wordcloud
import source: .source

// ============================================================
// Public API: render a <chart> element into an SVG element
// ============================================================

pub fn render(chart_el) {
    let tag = name(chart_el);
    if (tag == 'hconcat' or tag == 'vconcat')
        render_concat(parse.parse_concat(chart_el))
    else if (tag == 'repeat')
        render_repeat(parse.parse_repeat(chart_el))
    else
        dispatch(parse.parse_chart(chart_el))
}

// render from a pre-parsed spec map (no element tree needed)
pub fn render_spec(spec) {
    if (spec is element) render(spec)
    else if (spec.concat) render_concat(spec)
    else if (spec.repeat_row or spec.repeat_column) render_repeat(spec)
    else dispatch(spec)
}

fn resolve_data(spec) {
    source.resolve(spec.data, spec.data_source, spec.datasets)
}

fn prepare_spec(spec) {
    let raw_data = resolve_data(spec);
    let data = if (raw_data is error) raw_data
        else if (not (raw_data is array)) error("chart: data must be an array")
        else transform.apply_transforms(raw_data, spec.transform, spec.datasets);
    let partition_fields = [for (field in [spec.facet.field,
        if (spec.facet.row is string) spec.facet.row else spec.facet.row.field,
        if (spec.facet.column is string) spec.facet.column else spec.facet.column.field] where field != null) field];
    let prepared = if (data is error) {data: data}
        else if (spec.layer != null) {data: data, encoding: spec.encoding}
        else transform.prepare_encoding(data, spec.encoding, partition_fields);
    {*:spec, data: prepared.data, encoding: prepared.encoding, transform: null}
}

fn dispatch(spec) => dispatch_prepared(prepare_spec(spec))

fn dispatch_prepared(resolved_spec) {
    if (resolved_spec.data is error) resolved_spec.data
    else if (resolved_spec.facet) render_faceted(resolved_spec)
    else if (resolved_spec.layer) render_layered(resolved_spec)
    else if (resolved_spec.mark and resolved_spec.mark.kind == "arc") render_arc(resolved_spec)
    else if (resolved_spec.mark and resolved_spec.mark.kind == "wordcloud") render_wordcloud(resolved_spec)
    else render_single(resolved_spec)
}

// Word clouds share chart data/composition while retaining their measured layout contract.
fn cloud_marks(data, encoding, options, width, height) {
    let text_ch = parse.get_channel(encoding, "text");
    let size_ch = parse.get_channel(encoding, "size");
    let color_ch = parse.get_channel(encoding, "color");
    let color_scale = if (color_ch and color_ch.field)
        scale.infer_color_scale(color_ch, data) else null;
    let words = [for (record in data)
        if (record is map or record is element) (
            let row = if (record is element) map(record) else record,
            {*:row,
                text: parse.channel_value(text_ch, row, row.text),
                weight: parse.channel_value(size_ch, row, row.weight),
                color: mark.appearance({encoding: encoding, color_scale: color_scale}, "color", row,
                    if (row.color != null) row.color else options.color),
                font_size: if (row.font_size != null) row.font_size else options.font_size
            })
        else record];
    // Chart entry points return value errors; preserve the direct API's validation diagnostic (S7.4.1–S7.4.2).
    let image: element | error = cloud.render(words, {*:options, width: width, height: height});
    image
}

fn render_wordcloud(spec) {
    let theme = cfg.resolve_theme(spec.config);
    let data = transform.apply_transforms(if (spec.data) spec.data else [], spec.transform);
    let lay = layout.compute_layout(spec, null, null, false, null);
    let image = cloud_marks(data, spec.encoding, cfg.mark_config(theme, spec.mark), lay.plot_w, lay.plot_h);
    if (image is error) image
    else {
        let result = assemble_svg(spec, lay, image, null, null, null, null, theme);
        <svg *:map(result), role: "img",
            'aria-label': if (spec.title) spec.title else "Word cloud",
            'data-unplaced': image["data-unplaced"],
            for (child in content(result)) child>
    }
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
    let mark_spec = cfg.mark_config(theme, spec.mark);
    let mark_type = mark_spec.kind;
    let data_transformed = data_transformed0;
    let x_ch = parse.get_channel(enc, "x");
    let y_ch = parse.get_channel(enc, "y");
    let color_ch = parse.get_channel(enc, "color");
    let size_ch = parse.get_channel(enc, "size");
    let opacity_ch = parse.get_channel(enc, "opacity");
    let text_ch = parse.get_channel(enc, "text");
    let x_offset_ch = parse.get_channel(enc, "x_offset");
    let y2_ch = parse.get_channel(enc, "y2");
    let x2_ch = parse.get_channel(enc, "x2");
    let detail_ch = parse.get_channel(enc, "detail");
    let tooltip_ch = parse.get_channel(enc, "tooltip");

    let x_field = if (x_ch) x_ch.field else null;
    let y_field = if (y_ch) y_ch.field else null;
    let color_field0 = if (color_ch) color_ch.field else null;
    let x_offset_field = if (x_offset_ch) x_offset_ch.field else null;
    let y2_field = if (y2_ch) y2_ch.field else null;
    let x2_field = if (x2_ch) x2_ch.field else null;
    let detail_field = if (detail_ch) detail_ch.field else null;
    let tooltip_field = if (tooltip_ch) tooltip_ch.field else null;

    // determine if categorical/quantitative for position scales
    let x_type = if (x_ch) x_ch.dtype else "nominal";
    let y_type = if (y_ch) y_ch.dtype else "quantitative";

    let horizontal = mark_type == "bar" and x_type == "quantitative" and (y_type == "nominal" or y_type == "ordinal");
    let measure = if (horizontal) x_ch else y_ch;
    let has_range = if (horizontal) x2_ch != null else y2_ch != null;
    let stack_mode = if (has_range and measure.stack == null) null
        else detect_stack_mode(mark_type, measure, color_field0, x_offset_field);
    let ordered = if (enc.order.field != null) sort(data_transformed,
        {by: (row) => row[enc.order.field], dir: if (enc.order.sort == "descending") "desc" else "asc"})
        else data_transformed;
    let stack_order = if (color_ch.sort is array) color_ch.sort
        else if (color_ch.scale.domain != null) color_ch.scale.domain else null;
    let data_stacked = if (stack_mode and x_field and y_field and color_field0)
        stack.apply_stack(ordered, if (horizontal) x_field else y_field, color_field0,
            if (horizontal) y_field else x_field, stack_mode, stack_order)
        else ordered;

    let data = data_stacked;
    let color_field = color_field0;

    // x_offset categories for grouped bars
    let x_offset_cats = if (x_offset_field)
        util.unique_vals(data |> ~[x_offset_field])
    else null;

    // Conditional constants bypass this mapping in the mark renderer.
    let color_scale = if (color_ch and color_ch.field) scale.infer_color_scale(color_ch, data)
        else null;
    let color_categories = guide_values(color_scale);
    let has_legend = color_categories != null and len(color_categories) > 0 and parse.option_enabled(color_ch, "legend");

    // compute layout (need rough scales first for axis size estimation)
    let temp_x_scale = if (stack_mode and horizontal) build_stacked_y_scale(data, 0.0, float(spec.width), stack_mode)
        else scale.position_scale(x_ch, data, 0.0, float(spec.width), mark_type, true, x2_ch);
    let temp_y_scale = if (stack_mode and not horizontal)
        build_stacked_y_scale(data, float(spec.height), 0.0, stack_mode)
    else build_position_scale_y2(y_ch, data, float(spec.height), 0.0, mark_type, y2_ch);
    let lay = layout.compute_layout(spec, temp_x_scale, temp_y_scale, has_legend, color_categories);

    // rebuild scales with actual plot dimensions
    let x_scale = if (stack_mode and horizontal) build_stacked_y_scale(data, 0.0, lay.plot_w, stack_mode)
        else scale.position_scale(x_ch, data, 0.0, lay.plot_w, mark_type, true, x2_ch);
    let y_scale = if (stack_mode and not horizontal)
        build_stacked_y_scale(data, lay.plot_h, 0.0, stack_mode)
    else build_position_scale_y2(y_ch, data, lay.plot_h, 0.0, mark_type, y2_ch);

    // The same context resolves every visual encoding in single and layered views.
    let mark_ctx = mark_context(data, enc, x_scale, y_scale, lay, stack_mode);
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

    let x_axis_el = if (x_scale) axis.x_axis(x_scale, lay.plot_w, lay.plot_h, cfg.axis_config(theme, x_ch), x_title) else null;
    let y_axis_el = if (y_scale) axis.y_axis(y_scale, lay.plot_w, lay.plot_h, cfg.axis_config(theme, y_ch), y_title) else null;

    let x_guide = cfg.axis_config(theme, x_ch);
    let y_guide = cfg.axis_config(theme, y_ch);
    let x_grid = if (x_scale and x_guide.enabled and x_guide.grid and parse.has_attribute(x_ch.axis, "grid")) axis.x_axis_grid(x_scale, lay.plot_w, lay.plot_h, x_guide) else null;
    let y_grid = if (y_scale and y_guide.enabled and y_guide.grid) axis.y_axis_grid(y_scale, lay.plot_w, lay.plot_h, y_guide) else null;
    let y_grid_el = if (x_grid and y_grid) svg.group_class("grids", [x_grid, y_grid])
        else if (x_grid) x_grid else y_grid;

    // render legend
    let legend_el = if (has_legend)
        color_legend(color_ch, color_scale, color_categories, theme)
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

    let layer_specs = prepare_layers(layers, spec);
    let data_error = util.first_error(layer_specs |> ~.data);
    if (data_error is error) data_error else {
    let enc = if (len(layer_specs) > 0) layer_specs[0].encoding else {};
    let all_data = [for (layer in layer_specs where not (layer.data is error)) for (row in layer.data) row];

    let x_ch = parse.get_channel(enc, "x");
    let y_ch = parse.get_channel(enc, "y");
    // color channel: use parent encoding first, else pick from first layer that has color
    let parent_color_ch = parse.get_channel(enc, "color");
    let all_layer_color_chs = [for (ls in layer_specs, let l_enc = ls.encoding) parse.get_channel(l_enc, "color")];
    let first_layer_color_ch = (all_layer_color_chs |: (~ != null))[0];
    let color_ch = if (parent_color_ch) parent_color_ch else first_layer_color_ch;
    let x_field = if (x_ch) x_ch.field else null;
    let y_field = if (y_ch) y_ch.field else null;
    let color_field = if (color_ch) color_ch.field else null;
    let mark_type = if (layer_specs[0].mark) layer_specs[0].mark.kind else "line";

    let color_scale = if (color_ch) scale.infer_color_scale(color_ch, all_data) else null;
    let color_categories = guide_values(color_scale);
    let has_legend = color_categories != null and len(color_categories) > 0 and parse.option_enabled(color_ch, "legend");

    let temp_x_scale = layer_position_scale(layer_specs, "x", 0.0, float(spec.width));
    let temp_y_scale = layer_position_scale(layer_specs, "y", float(spec.height), 0.0);
    let lay = layout.compute_layout(spec, temp_x_scale, temp_y_scale, has_legend, color_categories);
    let x_scale = layer_position_scale(layer_specs, "x", 0.0, lay.plot_w);
    let y_scale = layer_position_scale(layer_specs, "y", lay.plot_h, 0.0);

    let layer_marks = [for (layer in layer_specs) (
        let layer_theme = cfg.resolve_theme(layer.config),
        let options = cfg.mark_config(layer_theme, layer.mark),
        let base_context = mark_context(layer.data, layer.encoding, x_scale, y_scale, lay, null),
        let context = {*:base_context, color_scale: if (layer.encoding.color.field == color_field and color_field != null)
            color_scale else base_context.color_scale},
        if (layer.data is error) layer.data
        else if (options.kind == "wordcloud") cloud_marks(layer.data, layer.encoding, options, lay.plot_w, lay.plot_h)
        else render_mark(options.kind, layer.data, context, options))];

    // axes
    let x_title = if (x_ch and x_ch.title) x_ch.title
        else if (x_field) x_field else null;
    let y_title = if (y_ch and y_ch.title) y_ch.title
        else if (y_field) y_field else null;

    let x_axis_el = if (x_scale) axis.x_axis(x_scale, lay.plot_w, lay.plot_h, cfg.axis_config(theme, x_ch), x_title) else null;
    let y_axis_el = if (y_scale) axis.y_axis(y_scale, lay.plot_w, lay.plot_h, cfg.axis_config(theme, y_ch), y_title) else null;

    // legend
    let legend_el = if (has_legend)
        color_legend(color_ch, color_scale, color_categories, theme)
    else null;

    // assemble with all layer marks
    let all_marks = svg.group_class("marks layers", layer_marks);
    let failure = util.first_error(layer_marks);
    if (failure is error) failure
    else assemble_svg(spec, lay, all_marks, x_axis_el, y_axis_el, null, legend_el, theme)
    }
}

// Resolve dataflow at each layer boundary before flattening leaves into the shared plot.
fn prepare_layers(layers, parent) {
    [for (layer in layers,
        let datasets = {*:parse.attributes(parent.datasets), *:parse.attributes(layer.datasets)},
        let own_data = layer.data != null or layer.data_source.values != null or
            layer.data_source.name != null or layer.data_source.url != null,
        let raw = if (own_data) resolve_data({*:layer, datasets: datasets}) else parent.data,
        let transformed = if (raw is error) raw else if (not (raw is array)) error("chart: data must be an array")
            else transform.apply_transforms(raw, layer.transform, datasets),
        let encoding = {*:cfg.settings(parent.encoding), *:cfg.settings(layer.encoding)},
        let inherited = {*:layer, data: transformed, encoding: encoding, datasets: datasets,
            config: cfg.inherit(parent.config, layer.config), transform: null},
        let prepared = if (transformed is error) {data: transformed}
            else if (layer.layer != null) null else transform.prepare_encoding(transformed, encoding),
        let leaves = if (transformed is error) [inherited]
            else if (layer.layer != null) prepare_layers(layer.layer, inherited)
            else [{*:inherited, data: prepared.data, encoding: prepared.encoding}])
        for (leaf in leaves) leaf]
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
    let mark_spec = cfg.mark_config(theme, spec.mark);
    let theta_ch = parse.get_channel(enc, "theta");
    let color_ch = parse.get_channel(enc, "color");
    let theta_field = if (theta_ch) theta_ch.field else null;
    let color_field = if (color_ch) color_ch.field else null;

    // color scale
    let color_scale = if (color_ch) scale.infer_color_scale(color_ch, data) else null;
    let color_categories = guide_values(color_scale);
    let has_legend = color_categories != null and len(color_categories) > 0 and parse.option_enabled(color_ch, "legend");

    // layout
    let lay = layout.compute_arc_layout(spec, has_legend, color_categories);
    let outer_r = if (mark_spec.outer_radius) float(mark_spec.outer_radius) else lay.radius;
    let inner_r = if (mark_spec.inner_radius)
        (let raw_ir = float(mark_spec.inner_radius),
         // clamp to at most 75% of outer radius to ensure visible ring width
         if (raw_ir >= outer_r * 0.75) outer_r * 0.6 else raw_ir)
    else 0.0;

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
        color_legend(color_ch, color_scale, color_categories, theme)
    else null;

    // assemble
    let width = lay.total_w;
    let height = lay.total_h;
    let bg = <rect width: width, height: height, fill: theme.background>;

    let children0 = [bg, arcs_el];

    // title
    let children1 = if (spec.title)
        [*children0,
         <text x: lay.total_w / 2.0, y: lay.title_y,
               'text-anchor': "middle", 'font-size': theme.title_font_size, 'font-weight': "bold", fill: theme.title_color,
             spec.title
         >]
    else children0;

    // legend
    let children = if (legend_el)
        [*children1,
         <g transform: svg.translate(lay.legend_x, lay.legend_y), legend_el>]
    else children1;

    svg.svg_root(width, height, children, cfg.svg_attributes(theme))
}

// ============================================================
// Build a position scale (x or y)
// ============================================================

fn build_position_scale(channel, data, rlo, rhi, mark_type: string, is_x: bool) {
    scale.position_scale(channel, data, rlo, rhi, mark_type, is_x)
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
    let y0_vals = data |> float(~["_y0"])
    let y1_vals = data |> float(~["_y1"])
    let all_vals = [*y0_vals, *y1_vals]
    let include_zero = mode != "center"
    scale.linear_scale_nice(all_vals, rlo, rhi, include_zero)
}

// build y scale that also considers y2_field for dual-value marks
fn build_position_scale_y2(y_ch, data, rlo, rhi, mark_type, y2_field) {
    scale.position_scale(y_ch, data, rlo, rhi, mark_type, false, y2_field)
}

fn mark_context(data, encoding, x_scale, y_scale, lay, stack_mode) {
    let color_ch = encoding.color;
    let stroke_ch = encoding.stroke;
    let shape_ch = encoding.shape;
    let offset_field = encoding.x_offset.field;
    {encoding: encoding, x_scale: x_scale, y_scale: y_scale,
        x_type: encoding.x.dtype, y_type: encoding.y.dtype,
        x_field: encoding.x.field, y_field: encoding.y.field,
        x2_field: encoding.x2.field, y2_field: encoding.y2.field,
        plot_w: lay.plot_w, plot_h: lay.plot_h,
        color_field: color_ch.field,
        color_scale: if (color_ch.field != null) scale.infer_color_scale(color_ch, data) else null,
        stroke_scale: if (stroke_ch.field != null) scale.infer_color_scale(stroke_ch, data) else null,
        shape_scale: if (shape_ch.field != null and parse.option_enabled(shape_ch, "scale"))
            scale.ordinal_scale(util.unique_vals(data |> ~[shape_ch.field]),
                if (shape_ch.scale.range != null) shape_ch.scale.range else ["circle", "square", "diamond", "triangle-up", "cross", "triangle-down"]) else null,
        size_field: encoding.size.field, opacity_field: encoding.opacity.field,
        size_scale: visual_scale(encoding.size, data, 20.0, 200.0),
        opacity_scale: visual_scale(encoding.opacity, data, 0.2, 1.0),
        text_field: encoding.text.field, detail_field: encoding.detail.field,
        tooltip_field: encoding.tooltip.field, is_stacked: stack_mode != null,
        x_offset_field: offset_field,
        x_offset_cats: if (offset_field != null) util.unique_vals(data |> ~[offset_field]) else null}
}

// Union values across each layer's own records and both endpoints before deriving shared scales.
fn layer_position_scale(layers, channel_name, rlo, rhi) {
    let candidates = [for (layer in layers,
        let channel = layer.encoding[channel_name]
        where channel != null and (channel.field != null or channel.datum != null))
        {channel: channel, mark: layer.mark, data: layer.data}];
    if (len(candidates) == 0) null
    else {
        let first = candidates[0];
        let channel = first.channel;
        let initial = scale.position_scale(channel, first.data, rlo, rhi, first.mark.kind, channel_name == "x");
        let values = [for (layer in layers where not (layer.data is error))
            for (key in [channel_name, channel_name ++ "2"],
                let current = layer.encoding[key]
                where current != null and current.value == null)
                for (row in layer.data,
                    let value = parse.channel_value(current, row)
                    where value != null) value];
        let has_ranges = len([for (layer in layers where layer.encoding[channel_name ++ "2"] != null) true]) > 0;
        let zero = if (channel.zero != null) channel.zero
            else not has_ranges and len([for (candidate in candidates where candidate.mark.kind == "bar") true]) > 0;
        if (initial.kind == "identity") initial
        else scale.configured_scale(values, rlo, rhi, initial.kind, channel.scale, zero)
    }
}

fn guide_values(mapping) => if (mapping != null and mapping.kind != "identity") mapping.domain else null

fn color_legend(channel, mapping, values, theme) {
    let options = cfg.legend_config(theme, channel);
    let title = if (not options.title_enabled) null else if (options.title != null) options.title
        else if (channel.title != null) channel.title else channel.field;
    if (mapping.kind == "sequential-color") leg.gradient_legend(mapping, title, options)
    else leg.color_legend(values, mapping, title, options)
}

fn visual_scale(channel, data, rlo, rhi) {
    if (channel == null or channel.field == null) null
    else if (not parse.option_enabled(channel, "scale")) {kind: "identity"}
    else scale.configured_scale(data |> ~[channel.field], rlo, rhi, "linear", channel.scale)
}

// ============================================================
// Dispatch mark rendering
// ============================================================

fn render_mark(mark_type, data, ctx, mark_spec) {
    if (mark_type == "bar")
        (if (ctx.x_type == "quantitative" and (ctx.y_type == "nominal" or ctx.y_type == "ordinal"))
            mark.bar_horizontal(data, ctx, mark_spec) else mark.bar(data, ctx, mark_spec))
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
    // Preserve fractional dimensions for measured marks and SVG viewports.
    let width = lay.total_w;
    let height = lay.total_h;

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
               'text-anchor': "middle", 'font-size': theme.title_font_size, 'font-weight': "bold", fill: theme.title_color,
             spec.title
         >]
    else children0;

    // legend
    let children = if (legend_el)
        [*children1,
         <g transform: svg.translate(lay.legend_x, lay.legend_y), legend_el>]
    else children1;

    svg.svg_root(width, height, children, cfg.svg_attributes(theme))
}

// ============================================================
// Faceted chart rendering (small multiples)
// ============================================================

fn render_faceted(spec) {
    let theme = cfg.resolve_theme(spec.config);
    let raw_data = if (spec.data) spec.data else [];
    let data = transform.apply_transforms(raw_data, spec.transform);
    let facet = spec.facet;

    let row_field = if (facet.row is string) facet.row else facet.row.field;
    let column_field = if (facet.column is string) facet.column else facet.column.field;
    let two_fields = row_field != null or column_field != null;
    let row_keys = if (row_field != null) util.unique_vals(data |> ~[row_field]) else [null];
    let column_keys = if (column_field != null) util.unique_vals(data |> ~[column_field]) else [null];
    let facet_keys = if (two_fields) [for (row in row_keys) for (column in column_keys) [row, column]]
        else util.unique_vals(data |> ~[facet.field]);
    let columns = if (two_fields) len(column_keys) else if (facet.columns != null) facet.columns else 3;
    let n = len(facet_keys);
    let sub_w = float(spec.width);
    let sub_h = float(spec.height);
    let spacing = if (facet.spacing != null) facet.spacing else 20;
    let flay = layout.compute_facet_layout(n, columns, sub_w, sub_h, spacing, spec.title, spec.padding);
    let encoding = shared_encoding(spec.encoding, data, spec.mark.kind, spec.resolve);
    let images = [for (key in facet_keys) (
        let cell_data = if (two_fields) (data |: (row_field == null or ~[row_field] == key[0]) and
            (column_field == null or ~[column_field] == key[1])) else (data |: ~[facet.field] == key),
        dispatch({*:spec, data: cell_data, facet: null, title: null, encoding: encoding}))];
    let failure = util.first_error(images);
    if (failure is error) failure else {
    let cells = [for (i in 0 to (n - 1),
        let key = facet_keys[i], let pos = layout.facet_cell_pos(flay, i))
        <g transform: svg.translate(pos.x, pos.y + flay.header_h),
            <text x: sub_w / 2.0, y: -4.0, 'text-anchor': "middle", 'font-size': 12, 'font-weight': "bold", fill: theme.title_color,
                if (two_fields) join([for (value in key where value != null) string(value)], " / ") else string(key)>
            images[i]>
    ];

    // background + title + cells
    let bg = <rect width: flay.total_w, height: flay.total_h, fill: theme.background>;
    let children0 = [bg, *cells];
    let children = if (spec.title)
        [*children0,
         <text x: flay.total_w / 2.0, y: float(spec.padding.top) + 16.0,
               'text-anchor': "middle", 'font-size': theme.title_font_size, 'font-weight': "bold", fill: theme.title_color,
             spec.title
         >]
    else children0;

    svg.svg_root(flay.total_w, flay.total_h, children)
    }
}

// ============================================================
// Concat composition (hconcat / vconcat)
// ============================================================

fn render_concat(spec) {
    let direction = spec.concat;
    let spacing = float(spec.spacing);
    let subs = spec.children;
    let n = len(subs);

    // render each child chart independently (skip if already SVG)
    let sub_svgs = [for (s in subs)
        if (s is element and name(s) == 'svg') s
        else render_spec(s)];

    let failure = util.first_error(sub_svgs);
    if (failure is error) failure else {

    // compute sizes from rendered SVGs
    let is_h = direction == "horizontal";
    let sizes = [for (s in sub_svgs) {
        w: if (s.width) float(s.width) else 400.0,
        h: if (s.height) float(s.height) else 300.0
    }];

    // accumulate offsets
    let items = position_subs(sizes, is_h, spacing, 0, 0.0, []);

    // total dimensions
    let total_w = if (is_h)
        max([for (p in items) p.x + p.w])
    else
        max([for (p in items) p.w]);
    let total_h = if (is_h)
        max([for (p in items) p.h])
    else
        max([for (p in items) p.y + p.h]);

    // wrap each sub-svg in a translated group
    let groups = [for (i in 0 to (n - 1))
        <g transform: svg.translate(items[i].x, items[i].y),
            sub_svgs[i]
        >];

    let bg = <rect width: total_w, height: total_h, fill: "white">;
    svg.svg_root(total_w, total_h, [bg, *groups])
    }
}

// recursive helper: accumulate positions for concat children
fn position_subs(sizes, is_h, spacing, idx, offset, acc) {
    if (idx >= len(sizes)) acc
    else (
        let s = sizes[idx],
        let entry = if (is_h)
            {x: offset, y: 0.0, w: s.w, h: s.h}
        else
            {x: 0.0, y: offset, w: s.w, h: s.h},
        let next_offset = if (is_h) offset + s.w + spacing
            else offset + s.h + spacing,
        position_subs(sizes, is_h, spacing, idx + 1, next_offset, [*acc, entry])
    )
}

// ============================================================
// Repeat composition (parameterized chart grid)
// ============================================================

fn render_repeat(spec) {
    let row_fields = if (spec.repeat_row) spec.repeat_row else [""];
    let col_fields = if (spec.repeat_column) spec.repeat_column else [""];
    let tmpl = spec.template;

    let n_rows = len(row_fields);
    let n_cols = len(col_fields);
    let n_total = n_rows * n_cols;

    // generate all charts in row-major order using flat index
    let sub_charts = [for (i in 0 to (n_total - 1),
                          let ri = int(i / n_cols),
                          let ci = i % n_cols,
                          let rf = row_fields[ri],
                          let cf = col_fields[ci])
        substitute_and_render(tmpl, rf, cf)];

    // build each row as an hconcat of its column charts
    let rows_svgs = [for (ri in 0 to (n_rows - 1),
                         let row_charts = [for (ci in 0 to (n_cols - 1)) sub_charts[ri * n_cols + ci]],
                         let row_spec = {concat: "horizontal", spacing: 10.0, children: row_charts})
        render_concat(row_spec)];

    if (n_rows == 1) rows_svgs[0]
    else render_concat({concat: "vertical", spacing: 10.0, children: rows_svgs})
}

// substitute {repeat: "row"} and {repeat: "column"} in template and render
fn substitute_and_render(tmpl, row_field, col_field) {
    let spec = if (tmpl is element) parse.parse_chart(tmpl) else tmpl;
    let prepared = prepare_spec(substitute_spec(spec, row_field, col_field));
    // Derived color fields need their transformed values before repeat domains are fixed.
    if (prepared.data is error) prepared.data
    else dispatch_prepared({*:prepared, encoding: shared_encoding(prepared.encoding, prepared.data, spec.mark.kind,
        {scale: {x: "independent", y: "independent"}})})
}

fn substitute_spec(spec, row_field, col_field) {
    {*:spec, encoding: substitute_encoding(spec.encoding, row_field, col_field),
        layer: if (spec.layer != null) [for (layer in spec.layer) substitute_spec(layer, row_field, col_field)] else null}
}

fn substitute_encoding(enc, row_field, col_field) {
    // Repeat fields apply to every encoding, including wordcloud text and size.
    map([for (key, channel in enc)
        for (value in [string(key), substitute_channel(channel, row_field, col_field)]) value])
}

// Fixed domains preserve comparisons and categorical colors across small multiples.
fn shared_encoding(encoding, data, mark_type, resolve) {
    map([for (key, channel in encoding,
        let channel_name = string(key),
        let independent = resolve.scale[channel_name] == "independent",
        let mapping = if (channel.field == null or independent or not parse.option_enabled(channel, "scale")) null
            else if (channel_name == "x" or channel_name == "y")
                scale.position_scale(channel, data, 0.0, 1.0, mark_type, channel_name == "x", encoding[channel_name ++ "2"])
            else if (channel_name == "color" or channel_name == "stroke") scale.infer_color_scale(channel, data)
            else if (channel_name == "size" or channel_name == "opacity") visual_scale(channel, data, 0.0, 1.0)
            else null)
        for (item in [channel_name, if (mapping != null and mapping.domain != null)
            {*:channel, scale: {*:parse.attributes(channel.scale), domain: mapping.domain}} else channel]) item])
}

fn substitute_channel(channel, row_field, col_field) {
    if (channel is array) [for (item in channel) substitute_channel(item, row_field, col_field)]
    else if (channel is map) map([for (key, value in channel) for (item in [string(key),
        if (string(key) == "field" and value.repeat != null)
            (if (value.repeat == "column") col_field else row_field)
        else substitute_channel(value, row_field, col_field)]) item])
    else channel
}

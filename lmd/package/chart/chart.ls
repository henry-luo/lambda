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
import records: .records
import text: .text
import paint: .paint

// ============================================================
// Public API: render a <chart> element into an SVG element
// ============================================================

pub fn render(chart_el) => render_scoped(chart_el, "chart")

fn render_scoped(chart_el, scope) {
    let tag = name(chart_el);
    if (tag == 'hconcat' or tag == 'vconcat')
        render_concat(parse.parse_concat(chart_el), scope)
    else if (tag == 'repeat')
        render_repeat(parse.parse_repeat(chart_el), scope)
    else
        dispatch(parse.parse_chart(chart_el), scope)
}

// render from a pre-parsed spec map (no element tree needed)
pub fn render_spec(spec) => render_spec_scoped(spec, "chart")

fn render_spec_scoped(spec, scope) {
    if (spec is element) render_scoped(spec, scope)
    else if (spec.concat) render_concat(spec, scope)
    else if (spec.repeat_row or spec.repeat_column) render_repeat(spec, scope)
    else dispatch(spec, scope)
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

fn dispatch(spec, scope = "chart") => dispatch_prepared({*:prepare_spec(spec), _paint_scope: scope})

fn dispatch_prepared(resolved_spec) {
    let mapping_error = if (resolved_spec.data is array and resolved_spec.layer == null)
        mapping_error(resolved_spec) else null;
    if (resolved_spec.data is error) resolved_spec.data
    else if (mapping_error is error) mapping_error
    else if (resolved_spec.facet) render_faceted(resolved_spec)
    else if (resolved_spec.layer) render_layered(resolved_spec)
    else {
        let paints = paint.plan(view_paints(resolved_spec), resolved_spec._paint_scope);
        let spec = {*:resolved_spec, _paints: paints};
        // reject invalid paints before measuring guides or laying out marks (S7.4.1).
        if (paints._error is error) paints._error
        else if (spec.mark and spec.mark.kind == "arc") render_arc(spec)
        else if (spec.mark and spec.mark.kind == "wordcloud") render_wordcloud(spec)
        else render_single(spec)
    }
}

fn mapping_error(spec) {
    let visual = mark_context(spec.data, spec.encoding, null, null, {}, null);
    util.first_error([visual.color_scale, visual.stroke_scale, visual.size_scale, visual.shape_scale,
        scale.position_scale(spec.encoding.x, spec.data, 0.0, 1.0, spec.mark.kind, true, spec.encoding.x2),
        scale.position_scale(spec.encoding.y, spec.data, 1.0, 0.0, spec.mark.kind, false, spec.encoding.y2)])
}

// resolve paint values after transforms, including conditional/identity colors and unused legend entries.
fn view_paints(spec) {
    let theme = cfg.resolve_theme(spec.config);
    let options = cfg.mark_config(theme, spec.mark);
    let context = mark_context(spec.data, spec.encoding, null, null, {}, null);
    [options.fill, options.color, options.stroke, theme.background, theme.title_color,
        for (row in spec.data) for (key in ["color", "stroke"]) mark.appearance(context, key, row,
            if (options.kind == "wordcloud" and key == "color") row.color else null),
        for (value in context.color_scale.range) value,
        for (key in ["color", "size", "shape"], let guide = cfg.legend_config(theme, spec.encoding[key]))
            for (value in [guide.symbol_fill_color, guide.symbol_stroke_color]) value,
        for (note in (if (spec.annotation != null) content(spec.annotation) else []))
            for (value in [note.color, note.stroke]) value]
}

// Word clouds share chart data/composition while retaining their measured layout contract.
fn cloud_marks(data, encoding, options, width, height, visual = null, paints = null) {
    let text_ch = parse.get_channel(encoding, "text");
    let size_ch = parse.get_channel(encoding, "size");
    let color_ch = parse.get_channel(encoding, "color");
    let color_scale = if (visual.color_scale != null) visual.color_scale else if (color_ch and color_ch.field)
        scale.infer_color_scale(color_ch, data) else null;
    let words = [for (record in data)
        if (record is map or record is element) (
            let row = if (record is element) map(record) else record,
            {*:row,
                text: parse.channel_value(text_ch, row, row.text),
                weight: parse.channel_value(size_ch, row, row.weight),
                color: paint.value(mark.appearance({encoding: encoding, color_scale: color_scale}, "color", row,
                    if (row.color != null) row.color else options.color), paints),
                font_size: if (row.font_size != null) row.font_size else options.font_size
            })
        else record];
    // Chart entry points return value errors; preserve the direct API's validation diagnostic (S7.4.1–S7.4.2).
    let image: element | error = cloud.render(words, {*:options, width: width, height: height});
    image
}

fn render_wordcloud(spec) {
    let paints = spec._paints;
    let theme = {*:cfg.resolve_theme(spec.config), _paints: paints};
    let data = transform.apply_transforms(if (spec.data) spec.data else [], spec.transform);
    let lay = layout.compute_layout(spec, null, null, false, null);
    let image = if (paints._error is error) paints._error else
        cloud_marks(data, spec.encoding, cfg.mark_config(theme, spec.mark), lay.plot_w, lay.plot_h, null, paints);
    if (image is error) image
    else {
        let result = assemble_svg(spec, lay, image, null, null, null, null, theme);
        if (result is error) result else <svg *:map(result), role: "img",
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
    let paints = spec._paints;
    let theme = {*:cfg.resolve_theme(spec.config), _paints: paints};
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

    let prepared_mark = prepare_mark_data({data: data_transformed, encoding: enc, mark: mark_spec});
    let horizontal = prepared_mark.stack_axis == "x";
    let stack_mode = prepared_mark.stack_mode;
    let data = prepared_mark.data;
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
    let guides = leg.plans(enc, mark_context(data, enc, null, null, {}, stack_mode), theme);

    // compute layout (need rough scales first for axis size estimation)
    let temp_x_scale = if (stack_mode and horizontal) build_stacked_y_scale(data, 0.0, float(spec.width), stack_mode, x_ch)
        else scale.position_scale(x_ch, data, 0.0, float(spec.width), mark_type, true, x2_ch);
    let temp_y_scale = if (stack_mode and not horizontal)
        build_stacked_y_scale(data, float(spec.height), 0.0, stack_mode, y_ch)
    else build_position_scale_y2(y_ch, data, float(spec.height), 0.0, mark_type, y2_ch);
    let lay = layout.compute_layout(spec, temp_x_scale, temp_y_scale, has_legend, color_categories, guides);

    // rebuild scales with actual plot dimensions
    let x_scale = if (stack_mode and horizontal) build_stacked_y_scale(data, 0.0, lay.plot_w, stack_mode, x_ch)
        else scale.position_scale(x_ch, data, 0.0, lay.plot_w, mark_type, true, x2_ch);
    let y_scale = if (stack_mode and not horizontal)
        build_stacked_y_scale(data, lay.plot_h, 0.0, stack_mode, y_ch)
    else build_position_scale_y2(y_ch, data, lay.plot_h, 0.0, mark_type, y2_ch);

    // The same context resolves every visual encoding in single and layered views.
    let mark_ctx = {*:mark_context(data, enc, x_scale, y_scale, lay, stack_mode), _paints: paints};
    let marks_el = render_mark(mark_type, data, mark_ctx, mark_spec);

    // render axes
    let x_title = if (x_ch and x_ch.title) x_ch.title
        else if (x_field) x_field else null;
    let y_title = if (y_ch and y_ch.title) y_ch.title
        else if (y_field) y_field else null;

    let notes = if (spec.annotation != null) [ann.prepare(spec.annotation, x_scale, y_scale,
        lay.plot_w, lay.plot_h, theme, spec.clip or theme.view_clip)] else [];
    let guide_layout = layout.resolve_labels([
        if (x_scale != null) {key: "x", mapping: x_scale, config: lay.x_axis_config, title: x_title},
        if (y_scale != null) {key: "y", mapping: y_scale, config: lay.y_axis_config, title: y_title}] |: ~ != null, notes, lay);
    let x_plan = (guide_layout.axes |: ~.key == "x")[0];
    let y_plan = (guide_layout.axes |: ~.key == "y")[0];
    let x_axis_el = if (x_scale) render_layer_axis(x_plan, lay) else null;
    let y_axis_el = if (y_scale) render_layer_axis(y_plan, lay) else null;
    let annotation_el = if (len(notes) > 0) ann.render_plan(guide_layout.annotations[0]) else null;
    let marks_with_ann = if (marks_el is error) marks_el else if (annotation_el is error) annotation_el
        else if (annotation_el != null) svg.group_class("plot-content", [marks_el, annotation_el]) else marks_el;

    let x_guide = cfg.axis_config(theme, x_ch);
    let y_guide = cfg.axis_config(theme, y_ch);
    let x_grid = if (x_scale and x_guide.enabled and x_guide.grid and parse.has_attribute(x_ch.axis, "grid")) axis.x_axis_grid(x_scale, lay.plot_w, lay.plot_h, x_guide) else null;
    let y_grid = if (y_scale and y_guide.enabled and y_guide.grid) axis.y_axis_grid(y_scale, lay.plot_w, lay.plot_h, y_guide) else null;
    let y_grid_el = if (x_grid and y_grid) svg.group_class("grids", [x_grid, y_grid])
        else if (x_grid) x_grid else y_grid;

    // render legend
    let legend_el = leg.render_plans(guides, lay);

    // assemble SVG
    assemble_svg(spec, lay, marks_with_ann, x_axis_el, y_axis_el, y_grid_el, legend_el, theme)
}

// ============================================================
// Layered chart rendering
// ============================================================

fn render_layered(spec) {
    let prepared = prepare_layers(spec.layer, spec);
    let data_error = util.first_error([for (layer in prepared)
        if (layer.data is error) layer.data else mapping_error(layer)]);
    if (data_error is error) data_error else {
        let layers = [for (layer in prepared) prepare_mark_data(layer)];
        let paints = paint.plan([*view_paints(spec), for (layer in layers) for (value in view_paints(layer)) value], spec._paint_scope);
        if (paints._error is error) paints._error else {
        let theme = {*:cfg.resolve_theme(spec.config), _paints: paints};
        let shared_visual = layer_visual_scales(layers);
        let guide_plans = layer_guide_plans(layers, shared_visual, spec.resolve, paints);
        let view_spec = {*:spec, encoding: if (len(layers) > 0) layers[0].encoding else {}};
        let rough = layer_scales(layers, spec.resolve, float(spec.width), float(spec.height));
        let rough_axes = layer_axes(layers, rough, spec.resolve);
        let lay = layout.compute_layout(view_spec, rough[0].x, rough[0].y, false, null, guide_plans, rough_axes);
        let mappings = layer_scales(layers, spec.resolve, lay.plot_w, lay.plot_h);
        let axes = [for (guide in rough_axes) {*:guide, mapping: mappings[guide.index][guide.key]}];
        let note_plans = [for (index, layer in layers) {*:ann.prepare(layer.annotation,
            mappings[index].x, mappings[index].y, lay.plot_w, lay.plot_h,
            {*:cfg.resolve_theme(layer.config), _paints: paints}, spec.clip or theme.view_clip)},
            {*:ann.prepare(spec.annotation, mappings[0].x, mappings[0].y, lay.plot_w, lay.plot_h,
                theme, spec.clip or theme.view_clip)}];
        let guide_layout = layout.resolve_labels(axes, note_plans, lay);
        let marks = [for (index, layer in layers) (
            let options = cfg.mark_config(cfg.resolve_theme(layer.config), layer.mark),
            let base = mark_context(layer.data, layer.encoding, mappings[index].x, mappings[index].y, lay, layer.stack_mode),
            let context = {*:resolved_visual_context(base, shared_visual, spec.resolve), _paints: paints},
            let image = if (options.kind == "wordcloud") cloud_marks(layer.data, layer.encoding, options, lay.plot_w, lay.plot_h, context, paints)
                else render_mark(options.kind, layer.data, context, options),
            // Preserve leaf diagnostics before annotations wrap the rendered value (S7.4.1).
            if (image is error) image
            else if (guide_layout.annotations[index]._error is error) guide_layout.annotations[index]._error
            else if (layer.annotation != null) svg.group_class("layer-content", [image,
                ann.render_plan(guide_layout.annotations[index])]) else image)];
        let failure = util.first_error([*marks, guide_layout.annotations[len(layers)]._error]);
        let axis_elements = [for (guide in guide_layout.axes) render_layer_axis(guide, lay)];
        let grids = [for (guide in axes where guide.config.grid)
            if (guide.key == "x") axis.x_axis_grid(guide.mapping, lay.plot_w, lay.plot_h, guide.config)
            else axis.y_axis_grid(guide.mapping, lay.plot_w, lay.plot_h, guide.config)];
        let notes = if (spec.annotation != null) ann.render_plan(guide_layout.annotations[len(layers)]) else null;
        let all_marks = svg.group_class("marks layers", [*marks, if (notes != null) notes]);
        if (failure is error) failure
        else assemble_svg(view_spec, lay, all_marks,
            if (len(axis_elements) > 0) axis_elements[0] else null,
            if (len(axis_elements) == 2) axis_elements[1]
            else if (len(axis_elements) > 2) svg.group_class("axes", slice(axis_elements, 1)) else null,
            if (len(grids) > 0) svg.group_class("grids", grids) else null, leg.render_plans(guide_plans, lay), theme)
        }
    }
}

// Shared position domains include stack endpoints; independent mappings retain each layer's policy.
fn layer_scales(layers, resolve, width, height) {
    let shared_x = if (resolve.scale.x != "independent") layer_position_scale(layers, "x", 0.0, width) else null;
    let shared_y = if (resolve.scale.y != "independent") layer_position_scale(layers, "y", height, 0.0) else null;
    [for (layer in layers) {
        x: if (resolve.scale.x == "independent") layer_position_scale([layer], "x", 0.0, width) else shared_x,
        y: if (resolve.scale.y == "independent") layer_position_scale([layer], "y", height, 0.0) else shared_y
    }]
}

fn layer_axes(layers, mappings, resolve) {
    let guides = [for (key in ["x", "y"],
        let independent = resolve.scale[key] == "independent" or resolve.axis[key] == "independent",
        let candidates = [for (index, layer in layers where layer.encoding[key] != null and mappings[index][key] != null)
            {index: index, layer: layer}],
        let selected = if (independent) candidates else slice(candidates, 0, 1))
        for (ordinal, candidate in selected,
            let channel = candidate.layer.encoding[key],
            let own_options = cfg.axis_config(cfg.resolve_theme(candidate.layer.config), channel),
            let mapping = mappings[candidate.index][key]
            where own_options.enabled and mapping.kind != "identity") (
            let title = if (channel.title != null) channel.title else channel.field,
            let orient = if (own_options.orient != null) own_options.orient else if (key == "x")
                (if (ordinal % 2 == 0) "bottom" else "top") else if (ordinal % 2 == 0) "left" else "right",
            let options = axis.prepare(mapping, {*:own_options, orient: orient}, title),
            let space = if (key == "x") axis.estimate_x_axis_height(options, title != null and options.title_enabled, mapping, title)
                else axis.estimate_y_axis_width(mapping, options, title),
            {key: key, index: candidate.index, mapping: mapping, title: title, orient: orient,
                config: {*:options, orient: orient}, space: space + abs(if (options.offset != null) options.offset else 0.0)})];
    [for (index, guide in guides) (
        let previous = sum([for (prior in slice(guides, 0, index) where prior.orient == guide.orient) prior.space]),
        let sign = if (guide.orient == "left" or guide.orient == "top") -1.0 else 1.0,
        {*:guide, config: {*:guide.config, offset: sign * previous +
            (if (guide.config.offset != null) guide.config.offset else 0.0)}})]
}

fn render_layer_axis(guide, lay) => if (guide.key == "x")
    axis.x_axis(guide.mapping, lay.plot_w, lay.plot_h, guide.config, guide.title)
    else axis.y_axis(guide.mapping, lay.plot_w, lay.plot_h, guide.config, guide.title)

fn layer_visual_scales(layers) {
    map([for (key in ["color", "size", "shape"],
        let candidates = [for (layer in layers where layer.encoding[key].field != null and
            (key == "color" or layer.mark.kind != "wordcloud")) layer],
        let first = candidates[0],
        let channel = first.encoding[key],
        let rows = [for (layer in candidates) for (row in layer.data)
            {value: parse.channel_value(layer.encoding[key], row)}],
        let normalized = {*:parse.attributes(channel), field: "value"},
        let mapping = if (len(candidates) == 0) null else if (key == "color") scale.infer_color_scale(normalized, rows)
            else if (key == "shape") shape_scale(normalized, rows) else visual_scale(normalized, rows, 20.0, 200.0))
        for (part in [key ++ "_scale", mapping]) part])
}

fn resolved_visual_context(context, shared, resolve) {
    {*:context, *:map([for (key in ["color", "size", "shape"]
        where resolve.scale[key] != "independent" and context.encoding[key].field != null and shared[key ++ "_scale"] != null)
        for (part in [key ++ "_scale", shared[key ++ "_scale"]]) part])}
}

fn layer_guide_plans(layers, shared, resolve, paints) {
    [for (key in ["color", "size", "shape"],
        // Word sizes use measured font layout, so symbol-area guides belong to Cartesian leaves.
        let candidates = [for (layer in layers where layer.encoding[key].field != null and layer.mark.kind != "wordcloud") layer],
        let independent = resolve.scale[key] == "independent" or resolve.legend[key] == "independent",
        let selected = if (independent) candidates else slice(candidates, 0, 1))
        for (layer in selected,
            let context = mark_context(layer.data, layer.encoding, null, null, {}, layer.stack_mode),
            let mappings = resolved_visual_context(context, shared, resolve))
            for (plan in leg.plans(map([key, layer.encoding[key]]), mappings,
                {*:cfg.resolve_theme(layer.config), _paints: paints})) plan]
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
    let paints = spec._paints;
    let theme = {*:cfg.resolve_theme(spec.config), _paints: paints};
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
    let guides = leg.plans(enc, {color_scale: color_scale}, theme);

    // layout
    let lay = layout.compute_arc_layout(spec, has_legend, color_categories, guides);
    let outer_r = if (mark_spec.outer_radius) float(mark_spec.outer_radius) else lay.radius;
    let inner_r = if (mark_spec.inner_radius)
        (let raw_ir = float(mark_spec.inner_radius),
         // clamp to at most 75% of outer radius to ensure visible ring width
         if (raw_ir >= outer_r * 0.75) outer_r * 0.6 else raw_ir)
    else 0.0;

    // render arcs
    let arc_ctx = {
        _paints: paints,
        encoding: enc,
        theta_field: theta_field,
        color_scale: color_scale, color_field: color_field,
        cx: lay.cx, cy: lay.cy,
        inner_radius: inner_r, outer_radius: outer_r
    };
    let arcs_el = mark.arc_mark(data, arc_ctx, mark_spec);

    // legend
    let legend_el = leg.render_plans(guides, lay);

    // assemble
    let width = lay.total_w;
    let height = lay.total_h;
    let bg = <rect width: width, height: height, fill: paint.value(theme.background, paints)>;

    let children0 = [paint.definitions(paints), bg, arcs_el];

    // title
    let children1 = if (spec.title)
        [*children0,
         layout.title_element(spec.title, lay, theme)]
    else children0;

    // legend
    let children = if (legend_el)
        [*children1,
         if (legend_el.class == "legends") legend_el else <g transform: svg.translate(lay.legend_x, lay.legend_y), legend_el>]
    else children1;

    let failure = util.first_error([paints._error, lay._error, arcs_el, legend_el]);
    if (failure is error) failure else svg.svg_root(width, height, children, cfg.svg_attributes(theme))
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

fn build_stacked_y_scale(data, rlo, rhi, mode, channel = null) {
    let y0_vals = data |> float(~["_y0"])
    let y1_vals = data |> float(~["_y1"])
    let all_vals = [*y0_vals, *y1_vals]
    let include_zero = mode != "center"
    scale.configured_scale(all_vals, rlo, rhi, "linear", channel.scale, include_zero)
}

// Ordering and stacking must be identical in single views and layer leaves.
fn prepare_mark_data(spec) {
    let enc = spec.encoding;
    let horizontal = spec.mark.kind == "bar" and enc.x.dtype == "quantitative" and
        (enc.y.dtype == "nominal" or enc.y.dtype == "ordinal");
    let measure = if (horizontal) enc.x else enc.y;
    let ranged = if (horizontal) enc.x2 != null else enc.y2 != null;
    let mode = if (ranged and measure.stack == null) null
        else detect_stack_mode(spec.mark.kind, measure, enc.color.field, enc.x_offset.field);
    let ordered = if (enc.order.field != null) sort(spec.data,
        {by: (row) => row[enc.order.field], dir: if (enc.order.sort == "descending") "desc" else "asc"}) else spec.data;
    let series_order = if (enc.color.sort is array) enc.color.sort else enc.color.scale.domain;
    let stacked = if (mode != null and enc.x.field != null and enc.y.field != null and enc.color.field != null)
        stack.apply_stack(ordered, measure.field, enc.color.field,
            if (horizontal) enc.y.field else enc.x.field, mode, series_order) else ordered;
    {*:spec, data: stacked, stack_mode: mode, stack_axis: if (horizontal) "x" else "y"}
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
        shape_scale: shape_scale(shape_ch, data),
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
            for (key in (if (layer.stack_mode != null and layer.stack_axis == channel_name) ["_y0", "_y1"] else [channel_name, channel_name ++ "2"]),
                let current = layer.encoding[key]
                where (current != null and current.value == null) or key == "_y0" or key == "_y1")
                for (row in layer.data,
                    let value = if (key == "_y0" or key == "_y1") row[key] else parse.channel_value(current, row)
                    where value != null) value];
        let has_ranges = len([for (layer in layers where layer.encoding[channel_name ++ "2"] != null) true]) > 0;
        let zero = if (channel.zero != null) channel.zero
            else not has_ranges and len([for (candidate in candidates where candidate.mark.kind == "bar") true]) > 0;
        // Rebuilding a shared domain must preserve the channel's resolved categorical order.
        let ordered = if (channel.sort != null and contains(["band", "point", "ordinal"], initial.kind))
            records.categories([for (value in values) {value: value}], "value", channel.sort) else values;
        if (initial.kind == "identity") initial
        else scale.configured_scale(ordered, rlo, rhi, initial.kind, channel.scale, zero)
    }
}

fn guide_values(mapping) => if (mapping != null and mapping.kind != "identity") mapping.domain else null

fn shape_scale(channel, data) {
    if (channel.field == null) null
    else if (not parse.option_enabled(channel, "scale")) {kind: "identity"}
    else scale.configured_scale(data |> ~[channel.field], 0, 1, "ordinal",
        {*:parse.attributes(channel.scale), range: if (channel.scale.range != null) channel.scale.range
            else ["circle", "square", "diamond", "triangle-up", "cross", "triangle-down"]})
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
    let image = render_mark_raw(mark_type, data, ctx, mark_spec);
    if (image is error) image else if (mark_spec.clip) clip_marks(image, ctx.plot_w, ctx.plot_h) else image
}

// A nested SVG viewport clips without document-global IDs that collide in compositions.
fn clip_marks(marks, width, height) => <svg class: "plot-clip", x: 0, y: 0, width: width, height: height,
    viewBox: "0 0 " ++ util.fmt_num(width) ++ " " ++ util.fmt_num(height), overflow: "hidden", marks>

fn render_mark_raw(mark_type, data, ctx, mark_spec) {
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
    let failure = util.first_error([theme._paints._error, lay._error, marks_el, x_axis_el, y_axis_el, legend_el]);
    if (failure is error) failure else {
    // Preserve fractional dimensions for measured marks and SVG viewports.
    let width = lay.total_w;
    let height = lay.total_h;

    // background
    let bg = <rect width: width, height: height, fill: paint.value(theme.background, theme._paints)>;

    // plot group contents
    let plot_children0 = [if (spec.clip or theme.view_clip) clip_marks(marks_el, lay.plot_w, lay.plot_h) else marks_el];
    let plot_children1 = if (grid_el) [grid_el, *plot_children0] else plot_children0;
    let plot_children2 = if (x_axis_el) [*plot_children1, x_axis_el] else plot_children1;
    let plot_children = if (y_axis_el) [*plot_children2, y_axis_el] else plot_children2;

    // plot group (translated by margins)
    let plot_group = svg.group(svg.translate(lay.plot_x, lay.plot_y), plot_children);

    let children0 = [paint.definitions(theme._paints), bg, plot_group];

    // title
    let children1 = if (spec.title)
        [*children0,
         layout.title_element(spec.title, lay, theme)]
    else children0;

    // legend
    let children = if (legend_el)
        [*children1,
         if (legend_el.class == "legends") legend_el else <g transform: svg.translate(lay.legend_x, lay.legend_y), legend_el>]
    else children1;

    svg.svg_root(width, height, children, cfg.svg_attributes(theme))
    }
}

// ============================================================
// Faceted chart rendering (small multiples)
// ============================================================

fn render_faceted(spec) {
    let base_theme = cfg.resolve_theme(spec.config);
    let paints = paint.plan([base_theme.background, base_theme.title_color], spec._paint_scope);
    let theme = {*:base_theme, _paints: paints};
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
    let headers = [for (key in facet_keys) if (two_fields)
        join([for (value in key where value != null) string(value)], " / ") else string(key)];
    let flay = layout.compute_facet_layout(n, columns, sub_w, sub_h, spacing, spec.title, spec.padding, theme, headers);
    let encoding = shared_encoding(spec.encoding, data, spec.mark.kind, spec.resolve);
    let images = [for (index, key in facet_keys) (
        let cell_data = if (two_fields) (data |: (row_field == null or ~[row_field] == key[0]) and
            (column_field == null or ~[column_field] == key[1])) else (data |: ~[facet.field] == key),
        dispatch({*:spec, data: cell_data, facet: null, title: null, encoding: encoding},
            spec._paint_scope ++ "-f" ++ string(index)))];
    let failure = util.first_error([paints._error, flay._error, *images]);
    if (failure is error) failure else {
    let cells = [for (i in 0 to (n - 1),
        let key = facet_keys[i], let pos = layout.facet_cell_pos(flay, i))
        <g transform: svg.translate(pos.x, pos.y + flay.header_h),
            <text x: sub_w / 2.0, y: 0.0 - flay.headers[i].bottom - 4.0, 'text-anchor': "middle",
                *:text.attributes(flay.header_font), fill: paint.value(theme.title_color, paints), headers[i]>
            images[i]>
    ];

    // background + title + cells
    let bg = <rect width: flay.total_w, height: flay.total_h, fill: paint.value(theme.background, paints)>;
    let children0 = [paint.definitions(paints), bg, *cells];
    let children = if (spec.title)
        [*children0,
         layout.title_element(spec.title, flay, theme)]
    else children0;

    svg.svg_root(flay.total_w, flay.total_h, children, cfg.svg_attributes(theme))
    }
}

// ============================================================
// Concat composition (hconcat / vconcat)
// ============================================================

fn render_concat(spec, scope) {
    let direction = spec.concat;
    let spacing = float(spec.spacing);
    let subs = spec.children;
    let n = len(subs);

    // render each child chart independently (skip if already SVG)
    let sub_svgs = [for (index, s in subs)
        if (s is element and name(s) == 'svg') s
        else render_spec_scoped(s, scope ++ "-c" ++ string(index))];

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

fn render_repeat(spec, scope) {
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
        substitute_and_render(tmpl, rf, cf, scope ++ "-r" ++ string(i))];

    // build each row as an hconcat of its column charts
    let rows_svgs = [for (ri in 0 to (n_rows - 1),
                         let row_charts = [for (ci in 0 to (n_cols - 1)) sub_charts[ri * n_cols + ci]],
                         let row_spec = {concat: "horizontal", spacing: 10.0, children: row_charts})
        render_concat(row_spec, scope ++ "-row" ++ string(ri))];

    if (n_rows == 1) rows_svgs[0]
    else render_concat({concat: "vertical", spacing: 10.0, children: rows_svgs}, scope)
}

// substitute {repeat: "row"} and {repeat: "column"} in template and render
fn substitute_and_render(tmpl, row_field, col_field, scope) {
    let spec = if (tmpl is element) parse.parse_chart(tmpl) else tmpl;
    let prepared = prepare_spec(substitute_spec(spec, row_field, col_field));
    // Derived color fields need their transformed values before repeat domains are fixed.
    if (prepared.data is error) prepared.data
    else dispatch_prepared({*:prepared, _paint_scope: scope, encoding: shared_encoding(prepared.encoding, prepared.data, spec.mark.kind,
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

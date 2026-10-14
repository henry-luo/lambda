// shared data preparation and mark dispatch for charts and timeline overlays.
import util: .util
import parse: .parse
import scale: .scale
import mark: .mark
import svg: .svg
import stack: .stack
import records: .records
import network: .network
import cluster: .cluster
import indicator: .indicator
import field: .field
import primitive: .primitive
import specialized: .specialized
import hierarchy: .hierarchy
import geo: .geo
import scientific: .scientific
import mesh: .mesh

// Pie and ranged arcs use identical scale, style, and radius validation in every view.
pub fn arc_context(data, encoding, options, width, height, paints, cx = null, cy = null) {
    let outer = if (options.outer_radius != null) options.outer_radius else min([width, height]) / 2.0;
    let inner = if (options.inner_radius != null) options.inner_radius else 0.0;
    let angles = scale.angular_scale(encoding.theta, data, encoding.theta2);
    let radii = scale.radius_scale(encoding.radius, data, 0.0, outer, encoding.radius2);
    let failure = util.first_error([angles, radii]);
    if (failure is error) failure
    else if (not util.finite_number(outer) or not util.finite_number(inner) or inner < 0 or outer < inner)
        error("chart: arc radii must be finite, nonnegative, and ordered")
    else {*:mark_context(data, encoding, null, null, {plot_w: width, plot_h: height}, null),
        _paints: paints, theta_field: encoding.theta.field, theta_scale: angles, radius_scale: radii,
        cx: if (cx != null) cx else width / 2.0, cy: if (cy != null) cy else height / 2.0,
        inner_radius: inner, outer_radius: outer}
}

fn detect_stack_mode(mark_type, y_ch, color_field, x_offset_field) {
    let s = if (y_ch) y_ch.stack else null
    if (s == "zero" or s == "normalize" or s == "center" or s == "wiggle") s
    else if (s == false or s == "none") null
    else if (s != null and s != true) error("chart: unsupported stack mode " ++ string(s))
    else if ((mark_type == "bar" or mark_type == "area") and color_field and not x_offset_field) "zero"
    else null
}

// Ordering and stacking must be identical in single views and layer leaves.
pub fn prepare_mark_data(spec) {
    if (spec._mark_prepared) spec
    else if (mesh.supports(spec.data) or scientific.supports(spec.mark.kind)) scientific.prepare(spec)
    else if (spec.mark.kind=="density") field.prepare(spec) else {
    let enc = spec.encoding;
    let horizontal = spec.mark.kind == "bar" and enc.x.dtype == "quantitative" and
        (enc.y.dtype == "nominal" or enc.y.dtype == "ordinal");
    let measure = if (horizontal) enc.x else enc.y;
    let ranged = if (horizontal) enc.x2 != null else enc.y2 != null;
    let mode = if (ranged and measure.stack == null) null
        else detect_stack_mode(spec.mark.kind, measure, enc.color.field, enc.x_offset.field);
    let ordered = if (enc.order.field != null) sort(spec.data,
        {by: (row) => row[enc.order.field], dir: if (enc.order.sort == "descending") "desc" else "asc"}) else spec.data;
    let series_order = if (measure.stack_order != null) measure.stack_order
        else if (enc.color.sort is array) enc.color.sort else enc.color.scale.domain;
    let position = if (horizontal) enc.y else enc.x;
    let positions = if (mode != "wiggle") null
        else if (position.dtype == "quantitative" or position.dtype == "temporal") sort(util.unique_vals(ordered |> ~[position.field]))
        else records.categories(ordered, position.field,
            if (position.scale.domain is array) position.scale.domain else position.sort);
    // Streamgraph validation errors remain values at both single and layer boundaries (S7.4.1).
    let stacked = if (mode is error) mode
        else if (positions is error) positions
        else if (mode == "wiggle" and (spec.mark.kind != "area" or enc.x.field == null or
            enc.y.field == null or enc.color.field == null)) error("chart: wiggle requires area marks with x, y, and color fields")
        else if (mode != null and enc.x.field != null and enc.y.field != null and enc.color.field != null)
        stack.apply_stack(ordered, measure.field, enc.color.field,
            position.field, mode, series_order, positions) else ordered;
    {*:spec, data: stacked, stack_mode: mode, stack_axis: if (horizontal) "x" else "y"}
    }
}

pub fn mark_context(data, encoding, x_scale, y_scale, lay, stack_mode) {
    let color_ch = encoding.color;
    let stroke_ch = encoding.stroke;
    let shape_ch = encoding.shape;
    let offset_field = encoding.x_offset.field;
    {data: data, encoding: encoding, x_scale: x_scale, y_scale: y_scale,
        x_type: encoding.x.dtype, y_type: encoding.y.dtype,
        x_field: encoding.x.field, y_field: encoding.y.field,
        x2_field: encoding.x2.field, y2_field: encoding.y2.field,
        plot_w: lay.plot_w, plot_h: lay.plot_h,
        color_field: color_ch.field,
        color_scale: if (color_ch.field != null or color_ch.datum != null) scale.infer_color_scale(color_ch, data) else null,
        stroke_scale: if (stroke_ch.field != null or stroke_ch.datum != null) scale.infer_color_scale(stroke_ch, data) else null,
        shape_scale: scale.shape_scale(shape_ch, data),
        size_field: encoding.size.field, opacity_field: encoding.opacity.field,
        size_scale: scale.visual_scale(encoding.size, data, 20.0, 200.0),
        opacity_scale: scale.visual_scale(encoding.opacity, data, 0.2, 1.0),
        text_field: encoding.text.field, detail_field: encoding.detail.field,
        tooltip_field: encoding.tooltip.field, is_stacked: stack_mode != null,
        x_offset_field: offset_field,
        x_offset_cats: if (offset_field != null) util.unique_vals(data |> ~[offset_field]) else null}
}

pub fn render(mark_type, data, ctx, mark_spec) {
    let image = render_mark_raw(mark_type, data, ctx, mark_spec);
    if (image is error) image else if (mark_spec.clip) clip_marks(image, ctx.plot_w, ctx.plot_h) else image
}

// A nested SVG viewport clips without document-global IDs that collide in compositions.
pub fn clip_marks(marks, width, height) => <svg class: "plot-clip", x: 0, y: 0, width: width, height: height,
    viewBox: "0 0 " ++ util.fmt_num(width) ++ " " ++ util.fmt_num(height), overflow: "hidden", marks>

fn render_mark_raw(mark_type, data, ctx, mark_spec) {
    if (scientific.supports(mark_type)) scientific.render(data,ctx,mark_spec)
    else if (contains(["sankey", "chord", "force_graph"], mark_type))
        network.render({*:ctx._graph, nodes: data}, ctx, mark_spec)
    else if (mark_type == "tree" or mark_type == "pack") cluster.render(data, ctx, mark_spec)
    else if (mark_type == "funnel") indicator.funnel(data, ctx, mark_spec)
    else if (mark_type == "gauge" or mark_type == "liquid") indicator.render(data, ctx, mark_spec)
    else if (mark_type == "density") field.render_density(data, ctx, mark_spec)
    else if (mark_type == "beeswarm" or (mark_type == "point" and mark_spec.beeswarm != null and mark_spec.beeswarm != false))
        field.render_swarm(data, ctx, {*:mark_spec, *:parse.attributes(mark_spec.beeswarm)})
    else if (contains(["link", "polygon", "path", "vector"], mark_type)) primitive.render(data, ctx, mark_spec)
    else if (mark_type == "bar")
        (if (ctx.x_type == "quantitative" and (ctx.y_type == "nominal" or ctx.y_type == "ordinal"))
            mark.bar_horizontal(data, ctx, mark_spec) else mark.bar(data, ctx, mark_spec))
    else if (mark_type == "line")
        mark.line_mark(data, ctx, mark_spec)
    else if (mark_type == "slope")
        mark.slope_mark(data, ctx, mark_spec)
    else if (mark_type == "trail")
        mark.trail_mark(data, ctx, mark_spec)
    else if (mark_type == "image")
        mark.image_mark(data, ctx, mark_spec)
    else if (mark_type == "violin")
        mark.violin_mark(data, ctx, mark_spec)
    else if (mark_type == "radar")
        specialized.radar(data, ctx, mark_spec)
    else if (mark_type == "parallel")
        specialized.parallel(data, ctx, mark_spec)
    else if (mark_type == "treemap" or mark_type == "sunburst")
        hierarchy.render(data, ctx, mark_spec)
    else if (mark_type == "geoshape" or mark_type == "geo")
        geo.render(data, ctx, mark_spec)
    else if (mark_type == "arc") (
        let arc = arc_context(data, ctx.encoding, mark_spec, ctx.plot_w, ctx.plot_h, ctx._paints),
        if (arc is error) arc else mark.arc_mark(data, {*:arc, _interactive: ctx._interactive, _view_path: ctx._view_path}, mark_spec))
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
    else error("chart: unsupported mark " ++ string(mark_type))
}

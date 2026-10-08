// chart/chart.ls — Main entry point for the Lambda Chart Library
// Provides chart.render(spec) -> SVG element tree

import parse: .parse
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
import records: .records
import text: .text
import paint: .paint
import sizing: .sizing
import composition: .composition
import specialized: .specialized
import hierarchy: .hierarchy
import geo: .geo
import parameter: .parameter
import interaction: .interaction
import events: .events
import network: .network
import cluster: .cluster
import field: .field
import indicator: .indicator
import primitive: .primitive
import coordinate: .coordinate
import behavior: .behavior
import animation: .animation
import timeline: .timeline

// ============================================================
// Public API: render a <chart> element into an SVG element
// ============================================================

pub fn render(chart_el, viewport = null) => render_spec(chart_el, viewport)

// render from a pre-parsed spec map (no element tree needed)
pub fn render_spec(spec, viewport = null, st = null) {
    if (spec is element and name(spec) == 'svg') spec else {
        let target = timeline.target(spec);
        if (target is error) target else render_chart(target, viewport, st)
    }
}

// Exports and live templates use this same explicit-time sampling entry point.
pub fn render_frame(raw, frame, viewport = null, st = null) {
    let spec = if (raw is element) parse.parse_top(raw) else raw;
    let elapsed = if (frame is number) frame else if (frame.time_ms != null) frame.time_ms
        else if (frame.elapsed != null) frame.elapsed else frame.time;
    let reduced = frame.reduced_motion == true;
    if (not util.finite_number(elapsed) or elapsed < 0) error("chart: frame time must be finite and nonnegative")
    else if (spec.timeline != null) {
        let segment = timeline.segment(spec, elapsed, frame.exact == true);
        // reduced motion freezes the terminal target instead of an intermediate keyframe.
        if (segment is error) segment else if (reduced) render_spec(spec, viewport, st) else {
            let source = render_chart({*:segment.source,_requires_key:segment.duration>0, _time: segment.elapsed, _timing_values: if (frame.timing_state != null) frame.timing_state.values else null}, viewport, if (frame.previous_state != null) frame.previous_state else st);
            let target = render_chart({*:segment.target,_requires_key:segment.duration>0, _time: segment.elapsed, _timing_values: if (frame.timing_state != null) frame.timing_state.values else null}, viewport, st);
            let image=if (source is error) source else if (target is error) target else
                animation.sample(source,target,segment.elapsed,{duration:segment.duration,timeline:true,progress:segment.progress});
            let duration=max([300.0,animation.duration(source),animation.duration(target)]);
            if (image is error) image else if (frame.previous!=null and frame.interrupted==true and elapsed<duration) {
                let visible=if (frame.previous.presentation!=null) frame.previous.presentation else frame.previous;
                animation.sample(visible,image,elapsed,{duration:duration})
            } else image
        }
    } else {
        let previous = if (frame.previous != null) frame.previous else null;
        let source = if (previous is element and name(previous) == 'svg') previous
            else if (previous.presentation != null) previous.presentation
            else if (previous != null) render_chart({*:if (previous.spec != null) previous.spec else previous, _requires_key:true, _time: elapsed, _timing_values: if (frame.timing_state != null) frame.timing_state.values else null}, viewport,
                if (frame.previous_state != null) frame.previous_state else if (previous.state!=null) previous.state else st) else null;
        let target = render_chart({*:spec,_requires_key:previous!=null and spec.animate!=false, _time: elapsed, _timing_values: if (frame.timing_state != null) frame.timing_state.values else null}, viewport, st);
        if (target is error) target else if (source is error) source else if (reduced) render_spec(spec, viewport, st)
        else animation.sample(source, target, elapsed, {duration: if (frame.duration != null) frame.duration else max([300.0, animation.duration(source), animation.duration(target)])})
    }
}

fn render_chart(raw, viewport, st) {
    let spec = behavior.prepare(raw,st);
    let context = sizing.viewport(viewport);
    let definitions = parameter.definitions(spec);
    let initial = parameter.reconcile(definitions,st);
    let values = if (initial is error) initial else parameter.values(definitions, initial);
    if (spec is error) spec else if (context is error) context else if (values is error) values else {
        let parsed = if (spec is element) parse.parse_top(spec) else spec;
        let prepared = composition.resolve(composition.prepare({*:parsed,
            _parameters: definitions, _parameter_state: initial, _parameter_values: values, _interactive: st != null}, prepare_mark_data));
        if (prepared is error) prepared else render_spec_scoped({*:prepared, _viewport: context}, "chart")
    }
}

// State belongs to this chart instance, never a module global or a closure (S9.1.4).
pub fn model(spec, viewport = null, options = {}) => <chart_view spec: spec, viewport: viewport, options: options>
pub fn interactive(spec, viewport = null, options = {}) => apply(model(spec, viewport, options))

view <chart_view> state interaction_state: null, playback_state: null {
    let definitions = parameter.definitions(behavior.prepare(~.spec,interaction_state));
    let st = parameter.reconcile(definitions,interaction_state);
    let image = if (st is error) st else playback_image(~, st, playback_state);
    if (image is error) image else <div class: "lambda-chart", tabindex: "0", 'data-chart-playback': if (playback_state != null) playback_state.phase else "idle",
        image;
        events.controls(definitions, st)
    >
}
on click(evt)       { interaction_state = chart_event(~, interaction_state, evt) }
on dblclick(evt)    { interaction_state = chart_event(~, interaction_state, evt) }
on pointerdown(evt) {
    interaction_state = chart_event(~, interaction_state, evt)
    if (interaction_state.gesture != null) { return 'prevent-default' }
    'pass'
}
on pointermove(evt) { interaction_state = chart_event(~, interaction_state, evt) }
on pointerup(evt)   { interaction_state = chart_event(~, interaction_state, evt) }
on pointercancel(evt) { interaction_state = chart_event(~, interaction_state, evt) }
on pointerover(evt) { interaction_state = chart_event(~, interaction_state, evt) }
on pointerout(evt)  { interaction_state = chart_event(~, interaction_state, evt) }
on pointerenter(evt) { interaction_state = chart_event(~, interaction_state, evt) }
on pointerleave(evt) { interaction_state = chart_event(~, interaction_state, evt) }
on mouseover(evt)   { interaction_state = chart_event(~, interaction_state, evt) }
on mouseout(evt)    { interaction_state = chart_event(~, interaction_state, evt) }
on mouseenter(evt)  { interaction_state = chart_event(~, interaction_state, evt) }
on mouseleave(evt)  { interaction_state = chart_event(~, interaction_state, evt) }
on mousemove(evt)   { interaction_state = chart_event(~, interaction_state, evt) }
on mousedown(evt)   { interaction_state = chart_event(~, interaction_state, evt) }
on mouseup(evt)     { interaction_state = chart_event(~, interaction_state, evt) }
on wheel(evt) {
    let before = interaction_state;
    interaction_state = chart_event(~, interaction_state, evt)
    if (interaction_state != before) { return 'prevent-default' }
    'pass'
}
on input(evt)  { interaction_state = chart_event(~, interaction_state, evt) }
on change(evt) { interaction_state = chart_event(~, interaction_state, evt) }
on keydown(evt) { interaction_state = chart_event(~, interaction_state, evt) }
on keyup(evt)   { interaction_state = chart_event(~, interaction_state, evt) }
on focusin(evt) { interaction_state = chart_event(~, interaction_state, evt) }
on focusout(evt) { interaction_state = chart_event(~, interaction_state, evt) }
on chart_parameter(evt) {
    let definitions = parameter.definitions(behavior.prepare(~.spec,interaction_state));
    let current = parameter.reconcile(definitions,interaction_state);
    let payload = if (evt.detail != null) evt.detail else evt;
    interaction_state = interaction.update(current, {*:payload, frame: {view: "chart", params: definitions}})
    if (interaction_state.values != current.values) { emit("chart_change", parameter.expression_values(parameter.values(definitions, interaction_state))) }
}

on chart_action(evt) {
    let compiled = behavior.prepare(~.spec,interaction_state);
    let definitions = parameter.definitions(compiled);
    let current = parameter.reconcile(definitions,interaction_state);
    let action = if (evt.detail != null) evt.detail else evt;
    if (contains(["play", "pause", "resume", "seek", "reverse", "cancel"], action.command)) {
        let previous = if (playback_state != null) playback_state else playback_initial(~, current);
        let plan = playback_plan(~.spec, previous.duration);
        let next = timeline.command(previous, action, evt.time_stamp, plan);
        if (next is error) { return next }
        playback_state = {*:next, presentation: playback_image(~, current, next)}
        if (next.notification != null) { emit("chart_animation", {phase: next.notification, elapsed: next.elapsed, position: next.position}) }
    } else {
        interaction_state = interaction.update(current, {*:action, type: "chart_action",
            frame: {view: "chart", params: definitions, behaviors: compiled._all_behaviors}})
        if (interaction_state.values != current.values) { emit("chart_change", parameter.expression_values(parameter.values(definitions, interaction_state))) }
        for (notice in interaction_state._notifications) { emit("chart_interaction", notice) }
        for (notice in interaction_state._custom_notifications) { emit(notice.name, notice.detail) }
    }
}

// A presentation commit queues this native frame event; an idle handler schedules no more work.
on render(evt) {
    if (evt.event_phase != 2) { return 'pass' }
    let definitions = parameter.definitions(behavior.prepare(~.spec,interaction_state));
    let current = parameter.reconcile(definitions,interaction_state);
    let previous = if (playback_state == null) playback_initial(~, current) else playback_state;
    let changed_input=previous.spec!=~.spec or previous.parameter_values!=current.values;
    let changed=previous.spec!=~.spec or changed_input and previous.target_signature!=animation.geometry_signature(render_spec(~.spec,~.viewport,current));
    let started = if (changed) playback_initial(~, current, previous.presentation) else previous;
    let plan = playback_plan(~.spec, started.duration);
    let next = if (started.phase == "playing") timeline.command(started, {command: "frame"}, evt.time_stamp, plan) else started;
    if (next is error) { return next }
    let newly_started = playback_state == null or changed;
    if (newly_started or previous.phase == "playing" or changed_input) {
        playback_state = {*:next, spec: ~.spec,parameter_values:current.values, presentation: playback_image(~, current, next)}
        if (newly_started and next.phase == "playing") { emit("chart_animation", {phase: "start", elapsed: 0.0}) }
        if (next.notification == "end") { emit("chart_animation", {phase: "end", elapsed: next.elapsed}) }
    }
    return 'handled'
}
fn has_animation(spec, phase = "enter") {
    let node = if (spec is element) parse.parse_top(spec) else spec;
    let options = animation.merge(node.animate, node.mark.animate);
    options != false and options[phase] != null and options[phase] != false or
        any([for (child in [*node.children, *node.layer, node.template] where child != null) has_animation(child, phase)])
}
fn playback_plan(raw, duration) {
    let spec=if (raw is element) parse.parse_top(raw) else raw;
    if (spec.timeline != null) timeline.configure(spec) else
        {duration: duration, length: duration, repeat: 1, direction: "normal", fill: "forwards"}
}
fn playback_initial(model, st, previous = null) {
    let spec=if (model.spec is element) parse.parse_top(model.spec) else model.spec;
    let target = render_spec(model.spec, model.viewport, st);
    let duration = max(animation.duration(target),animation.duration(previous));
    let has_timeline = spec.timeline != null;
    let plan = if (has_timeline) timeline.configure(model.spec) else null;
    let running = if (has_timeline) plan.autoplay else has_animation(model.spec, if (previous != null) "update" else "enter");
    {spec: model.spec, previous: previous, timing_state: st,parameter_values:st.values,target_signature:animation.geometry_signature(target),duration: duration, elapsed: 0.0, anchor: null,
        phase: if (running and model.options.reduced_motion != true) "playing" else if (has_timeline) "paused" else "ended",
        position: if (has_timeline and not running) 0.0 else null, reversed: false, generation: 0, presentation: previous}
}
fn playback_image(model, st, playback) {
    let clock = if (playback != null) playback else playback_initial(model, st);
    let spec=if (model.spec is element) parse.parse_top(model.spec) else model.spec;
    let changed=clock.spec!=model.spec or clock.parameter_values!=st.values and
        clock.target_signature!=animation.geometry_signature(render_spec(model.spec,model.viewport,st));
    let frame = {time_ms: if (clock.position != null) clock.position else clock.elapsed, exact: clock.position != null,
        previous: if (changed) clock.presentation else clock.previous, interrupted:changed or clock.previous!=null and spec.timeline!=null,
        previous_state: clock.timing_state, timing_state: clock.timing_state, reduced_motion: model.options.reduced_motion == true};
    if (changed and spec.timeline==null and not has_animation(spec,"update")) render_spec(model.spec,model.viewport,st)
    else if (changed) render_frame(model.spec, {*:frame, time_ms: 0.0}, model.viewport, st)
    else if (spec.timeline != null) render_frame(model.spec,if (clock.phase=="idle") {*:frame,previous:null,interrupted:false,time_ms:0.0} else frame,model.viewport,st)
    else if (clock.position!=null) render_frame(model.spec,frame,model.viewport,st)
    else if (clock.phase == "idle") if (clock.previous != null) clock.previous else render_frame(model.spec, {*:frame, time_ms: 0.0}, model.viewport, st)
    else if (clock.phase == "ended") if (clock.reversed and clock.elapsed==0) render_frame(model.spec,frame,model.viewport,st)
        else render_spec(model.spec,model.viewport,st)
    else render_frame(model.spec, frame, model.viewport, st)
}

pn chart_event(model, st, evt) {
    let compiled = behavior.prepare(model.spec,st);
    let definitions = parameter.definitions(compiled);
    let current = parameter.reconcile(definitions,st);
    let next = events.dispatch(current, evt, definitions, compiled._all_behaviors);
    if (next.values != current.values) { emit("chart_change", parameter.expression_values(parameter.values(definitions, next))) }
    for (notice in next._notifications) { emit("chart_interaction", notice) }
    for (notice in next._custom_notifications) { emit(notice.name, notice.detail) }
    next
}

fn render_spec_scoped(spec, scope) {
    if (spec is element) spec
    else if (spec.concat) render_concat(spec, scope)
    else if (spec.repeat_row or spec.repeat_column) render_repeat(spec, scope)
    else dispatch(spec, scope)
}

fn dispatch(spec, scope = "chart") {
    let sized = if (spec.layer != null or spec.facet != null) spec else sizing.resolve_view(spec);
    if (sized is error) sized else dispatch_prepared({*:sized, _paint_scope: scope})
}

fn dispatch_prepared(resolved_spec) {
    let mapping_error = if (resolved_spec.data is array and resolved_spec.layer == null and resolved_spec.facet == null)
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
        else if (coordinate.enabled(spec.coordinate)) render_coordinate(spec)
        else if (spec.mark and spec.mark.kind == "arc") render_arc(spec)
        else if (spec.mark and spec.mark.kind == "wordcloud") render_wordcloud(spec)
        else render_single(spec)
    }
}

fn mapping_error(spec) {
    let visual = mark_context(spec.data, spec.encoding, null, null, {}, null);
    util.first_error([visual.color_scale, visual.stroke_scale, visual.size_scale, visual.shape_scale, visual.opacity_scale,
        scale.position_scale(spec.encoding.x, spec.data, 0.0, 1.0, spec.mark.kind, true, spec.encoding.x2),
        scale.position_scale(spec.encoding.y, spec.data, 1.0, 0.0, spec.mark.kind, false, spec.encoding.y2)])
}

// resolve paint values after transforms, including conditional/identity colors and unused legend entries.
fn view_paints(spec) {
    let theme = cfg.resolve_theme(spec.config);
    let options = cfg.mark_config(theme, spec.mark);
    let context = mark_context(spec.data, spec.encoding, null, null, {}, null);
    [options.fill, options.color, options.stroke, theme.background, theme.title_color,
        for (threshold in options.thresholds) threshold.color,
        for (key,part in spec.mark.parts,
            let rows=if (string(key)=="link" and spec._graph!=null) spec._graph.links else spec.data,
            let visual=mark.part_context(context,options,string(key),rows)) (
            part.fill,part.color,part.stroke,
            for (row in rows) for (channel in ["color","stroke"]) mark.appearance(visual,channel,row,null),
            for (value in visual.color_scale.range) value),
        for (states in [spec.state,options.state,for (key,part in spec.mark.parts) part.state])
            for (key,style in states) for (value in [style.fill,style.color,style.stroke]) value,
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
                font_size: if (row.font_size != null) row.font_size else options.font_size,
                *:(if (visual._interactive == true) {_chart_attrs: parameter.target_attributes(visual, row)} else {})
            })
        else record];
    // Chart entry points return value errors; preserve the direct API's validation diagnostic (S7.4.1–S7.4.2).
    let image: element | error = cloud.render(words, {*:options, width: width, height: height});
    image
}

fn render_wordcloud(spec) {
    let paints = spec._paints;
    let theme = {*:cfg.resolve_theme(spec.config), _paints: paints};
    let data = spec.data;
    let lay = layout.compute_layout(spec, null, null, false, null);
    let image = if (paints._error is error) paints._error else
        cloud_marks(data, spec.encoding, cfg.mark_config(theme, spec.mark), lay.plot_w, lay.plot_h,
            {*:behavior.context(spec), _interactive: spec._interactive, _view_path: spec._view_path}, paints);
    if (image is error) image
    else {
        let result = assemble_svg(spec, lay, interaction.decorate(image, spec, lay), null, null, null, null, theme);
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
    let data_transformed0 = raw_data;

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

    let prepared_mark = spec;
    if (prepared_mark.data is error) prepared_mark.data else {
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
    let mark_ctx = {*:mark_context(data, enc, x_scale, y_scale, lay, stack_mode), _paints: paints, _theme: theme, _projection: spec.projection,
        *:behavior.context(spec), _interactive: spec._interactive, _view_path: spec._view_path, _graph: spec._graph};
    let marks_el = interaction.decorate(render_mark(mark_type, data, mark_ctx, mark_spec), spec, lay, x_scale, y_scale);

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
}

// ============================================================
// Layered chart rendering
// ============================================================

fn render_layered(spec) {
    if (coordinate.enabled(spec.coordinate)) render_coordinate(spec) else {
    let layers = composition.leaves(spec);
    let data_error = util.first_error([for (layer in layers)
        if (layer.data is error) layer.data else mapping_error(layer)]);
    if (data_error is error) data_error else {
        let view_spec = sizing.resolve_view({*:spec, data: [for (layer in layers) for (row in layer.data) row],
            encoding: if (len(layers) > 0) layers[0].encoding else {}});
        if (view_spec is error) view_spec else {
        let paints = paint.plan([*view_paints(spec), for (layer in layers) for (value in view_paints(layer)) value], spec._paint_scope);
        if (paints._error is error) paints._error else {
        let theme = {*:cfg.resolve_theme(spec.config), _paints: paints};
        let guide_plans = layer_guide_plans(layers, paints);
        let rough = layer_scales(layers, float(view_spec.width), float(view_spec.height));
        let rough_axes = layer_axes(layers, rough);
        let lay = layout.compute_layout(view_spec, rough[0].x, rough[0].y, false, null, guide_plans, rough_axes);
        let mappings = layer_scales(layers, lay.plot_w, lay.plot_h);
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
            let context = {*:base, _paints: paints, _theme: cfg.resolve_theme(layer.config),
                *:behavior.context(layer), _interactive: layer._interactive, _view_path: layer._view_path, _graph: layer._graph,
                _projection: if (layer.projection != null) layer.projection else spec.projection},
            let image = interaction.decorate(if (options.kind == "wordcloud") cloud_marks(layer.data, layer.encoding, options, lay.plot_w, lay.plot_h, context, paints)
                else render_mark(options.kind, layer.data, context, options), layer, lay, mappings[index].x, mappings[index].y, index == 0),
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
    }
}

// Shared position domains include stack endpoints; independent mappings retain each layer's policy.
fn layer_scales(layers, width, height) {
    [for (layer in layers) {
        x: scale.shared_position([layer], "x", 0.0, width),
        y: scale.shared_position([layer], "y", height, 0.0)
    }]
}

fn layer_axes(layers, mappings) {
    let guides = [for (key in ["x", "y"],
        let selected = [for (index, layer in layers where layer.encoding[key] != null and mappings[index][key] != null and
            cfg.axis_config(cfg.resolve_theme(layer.config), layer.encoding[key]).enabled)
            {index: index, layer: layer}])
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

fn layer_guide_plans(layers, paints) {
    [for (layer in layers where layer.mark.kind != "wordcloud")
        for (plan in leg.plans(layer.encoding, mark_context(layer.data, layer.encoding, null, null, {}, layer.stack_mode),
            {*:cfg.resolve_theme(layer.config), _paints: paints})) plan]
}

// ============================================================
// Arc (pie/donut) chart rendering
// ============================================================

fn render_arc(spec) {
    let paints = spec._paints;
    let theme = {*:cfg.resolve_theme(spec.config), _paints: paints};
    let legend_cfg = cfg.legend_config(theme);
    let raw_data = if (spec.data) spec.data else [];
    let data = raw_data;

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
    let arc_ctx0 = arc_context(data, enc, mark_spec, lay.plot_w, lay.plot_h, paints, lay.cx, lay.cy);
    let arc_ctx = if (arc_ctx0 is error) arc_ctx0 else {*:arc_ctx0, *:behavior.context(spec), _interactive: spec._interactive, _view_path: spec._view_path};
    let arcs_el = if (arc_ctx is error) arc_ctx else interaction.decorate(mark.arc_mark(data, arc_ctx, mark_spec), spec,
        {*:lay, plot_x: 0.0, plot_y: 0.0, plot_w: lay.total_w, plot_h: lay.total_h});

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
    if (failure is error) failure else svg.svg_root(width, height, children, {*:cfg.svg_attributes(theme), *:interaction_attributes(spec)})
}

// Pie and ranged arcs use identical scale, style, and radius validation in every view.
fn arc_context(data, encoding, options, width, height, paints, cx = null, cy = null) {
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

// ============================================================
// Stacking helpers
// ============================================================

fn detect_stack_mode(mark_type, y_ch, color_field, x_offset_field) {
    let s = if (y_ch) y_ch.stack else null
    if (s == "zero" or s == "normalize" or s == "center" or s == "wiggle") s
    else if (s == false or s == "none") null
    else if (s != null and s != true) error("chart: unsupported stack mode " ++ string(s))
    else if ((mark_type == "bar" or mark_type == "area") and color_field and not x_offset_field) "zero"
    else null
}

fn build_stacked_y_scale(data, rlo, rhi, mode, channel = null) {
    let y0_vals = data |> float(~["_y0"])
    let y1_vals = data |> float(~["_y1"])
    let all_vals = [*y0_vals, *y1_vals]
    let include_zero = mode != "center" and mode != "wiggle"
    scale.configured_scale(all_vals, rlo, rhi, "linear", channel.scale, include_zero)
}

// Ordering and stacking must be identical in single views and layer leaves.
fn prepare_mark_data(spec) {
    if (spec._mark_prepared) spec else if (spec.mark.kind=="density") field.prepare(spec) else {
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

// build y scale that also considers y2_field for dual-value marks
fn build_position_scale_y2(y_ch, data, rlo, rhi, mark_type, y2_field) {
    scale.position_scale(y_ch, data, rlo, rhi, mark_type, false, y2_field)
}

fn mark_context(data, encoding, x_scale, y_scale, lay, stack_mode) {
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

// Union values across each layer's own records and both endpoints before deriving shared scales.

fn guide_values(mapping) => if (mapping != null and mapping.kind != "identity") mapping.domain else null



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
    if (contains(["sankey", "chord", "force_graph"], mark_type))
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

// A coordinate view resolves all layers against one plot, with independent data scales.
fn render_coordinate(spec) {
    let layers = if (spec.layer != null) composition.leaves(spec) else [spec];
    let sized = sizing.resolve_view(spec);
    let paints = paint.plan([for (layer in layers) for (value in view_paints(layer)) value], spec._paint_scope);
    let theme = {*:cfg.resolve_theme(spec.config), _paints: paints};
    let guides = layer_guide_plans(layers, paints);
    let failure=util.first_error([sized,paints._error]);
    if (failure is error) failure else fit_coordinate(sized,layers,theme,paints,guides)
}
fn fit_coordinate(spec,layers,theme,paints,guides,reserved={left:0.0,right:0.0,top:0.0,bottom:0.0}) {
    let lay=layout.compute_layout(spec,null,null,false,null,guides);
    let model=coordinate.configure(spec.coordinate,lay.plot_w,lay.plot_h);
    let images=[for (layer in layers) coordinate_layer(layer,model,lay,theme,paints)];
    let image=svg.group_class("coordinate-layers",images);
    let bounds=text.svg_bounds(image,text.style({font_family:theme.font}));
    let failure=util.first_error([lay._error,model,*images,bounds]);
    let overflow={left:max([0.0,for (row in bounds) -row.left-reserved.left]),right:max([0.0,for (row in bounds) row.right-lay.plot_w-reserved.right]),
        top:max([0.0,for (row in bounds) -row.top-reserved.top]),bottom:max([0.0,for (row in bounds) row.bottom-lay.plot_h-reserved.bottom])};
    if (failure is error) failure else if (max([for (key,value in overflow) value])>0.25)
        fit_coordinate({*:spec,padding:map([for (key,value in spec.padding) for (part in [string(key),value+overflow[string(key)]]) part])},
            layers,theme,paints,guides,map([for (key,value in reserved) for (part in [string(key),value+overflow[string(key)]]) part]))
    else assemble_svg(spec,lay,image,null,null,null,leg.render_plans(guides,lay),theme)
}

fn coordinate_layer(spec, model, lay, theme, paints) {
    let enc = spec.encoding;
    let direct = enc.theta != null or enc.radius != null;
    let encoding = if (direct) {*:enc, x: enc.theta, x2: enc.theta2, y: enc.radius, y2: enc.radius2} else enc;
    let options = cfg.mark_config(theme, spec.mark);
    let x = coordinate.channel(encoding.x, lay.plot_w);
    let y = coordinate.channel(encoding.y, lay.plot_h, true);
    let xs = scale.position_scale(x, spec.data, 0.0, lay.plot_w, options.kind, true, encoding.x2);
    let ys = scale.position_scale(y, spec.data, lay.plot_h, 0.0, options.kind, false, encoding.y2);
    let spatial = {*:model, direct_polar: direct};
    let axes = if (contains(["parallel", "radar"], model.type) and (enc.position != null or model.type == "parallel"))
        coordinate.vector_axes(spec.data, if (enc.position != null) enc.position else options.fields, spatial) else null;
    let context = {*:mark_context(spec.data, encoding, xs, ys, lay, spec.stack_mode), _paints: paints, _theme: theme,
        *:behavior.context(spec), _interactive: spec._interactive, _view_path: spec._view_path, _graph: spec._graph,
        _projection: if (spec.projection != null) spec.projection else model.projection, _coordinate: spatial};
    let invalid = util.first_error([coordinate.compatible(options.kind, model), xs, ys,
        if (direct and (enc.x != null or enc.y != null)) error("chart: coordinate positions use x/y or theta/radius, not both")]);
    if (invalid is error) invalid else {
        let image = if (model.type == "parallel") coordinate.vector_marks(spec.data, {*:context, encoding: {*:encoding,
            position: if (enc.position != null) enc.position else options.fields}}, options, spatial)
            else if (model.type == "radar" and enc.position != null) coordinate.vector_marks(spec.data, context, options, spatial)
            else if (model.type == "radar" and enc.theta != null) specialized.radar(spec.data, context, options)
            else if (model.type=="geo" and options.kind=="vector") geo.vectors(spec.data,context,options)
            else if (options.kind == "arc") coordinate_arcs(spec.data, context, options, spatial)
            else if (options.kind == "wordcloud") cloud_marks(spec.data, encoding, options, lay.plot_w, lay.plot_h, context, paints)
            else render_mark(options.kind, spec.data, context, {*:options,clip:false});
        let projected = if (image is error) image else if (contains(["parallel", "radar", "geo"], model.type) or
            options.kind == "arc" or (options.kind == "path" and options.space != "coordinate")) image
            else coordinate.warp(image, spatial);
        let notes = if (spec.annotation == null) null else ann.render_plan(ann.prepare(spec.annotation, xs, ys,
            lay.plot_w, lay.plot_h, theme, spec.clip));
        let projected_notes = if (notes == null or notes is error) notes else coordinate.warp(notes, spatial);
        let guides=if (not contains(["parallel","radar","geo"],model.type)) coordinate.guides(spatial,encoding,xs,ys,theme) else null;
        let failure = util.first_error([projected, projected_notes,guides]);
        if (failure is error) failure else svg.group_class("coordinate-view", [guides,
            interaction.decorate(if (spec.clip or options.clip or theme.view_clip) coordinate.clip(projected, spatial, spec._paint_scope) else projected, {*:spec, encoding: encoding, _coordinate: spatial, _axis_models: axes}, lay, xs, ys), projected_notes])
    }
}

fn coordinate_arcs(data, ctx, options, model) {
    let weights = [for (row in data) parse.channel_value(ctx.encoding.theta, row)];
    let ranged = ctx.encoding.theta2 != null;
    let total = if (all(weights |> util.finite_number(~))) sum(weights) else null;
    let radii = scale.radius_scale(ctx.encoding.radius, data, model.inner_radius, model.outer_radius, ctx.encoding.radius2);
    let angles = scale.angular_scale(ctx.encoding.theta, data, ctx.encoding.theta2);
    let half=min(model.width,model.height)/2.0;
    let gap=if (options.pad_angle!=null) options.pad_angle/abs(model.end_angle-model.start_angle) else 0.0;
    let invalid = util.first_error([radii, angles,
        if (not util.finite_number(gap) or gap<0) error("chart: arc padding must be finite and nonnegative"),
        if (not ranged and (not util.finite_number(total) or any(weights |> ~ < 0))) error("chart: pie weights must be finite and nonnegative")]);
    if (invalid is error) invalid else svg.group_class("marks arcs", [for (i, row in data where ranged or (total > 0 and weights[i] > 0),
        let divisor=if (ctx.encoding.theta.scale.range!=null and parse.option_enabled(ctx.encoding.theta,"scale")) 1.0 else util.TAU,
        let first = if (ranged) scale.scale_apply(angles, parse.channel_value(ctx.encoding.theta, row)) / divisor else sum(slice(weights, 0, i)) / total,
        let end_fraction = if (ranged) scale.scale_apply(angles, parse.channel_value(ctx.encoding.theta2, row)) / divisor else first + weights[i] / total,
        let a=first+min(gap,end_fraction-first)/2.0,let b=end_fraction-min(gap,end_fraction-first)/2.0,
        let radial_divisor=if (parse.option_enabled(ctx.encoding.radius,"scale")) 1.0 else half,
        let inner = if (ctx.encoding.radius2 != null) scale.scale_apply(radii, parse.channel_value(ctx.encoding.radius2, row))/radial_divisor
            else if (options.inner_radius!=null) options.inner_radius/half else model.inner_radius,
        let outer = if (ctx.encoding.radius != null) scale.scale_apply(radii, parse.channel_value(ctx.encoding.radius, row))/radial_divisor
            else if (options.outer_radius!=null) options.outer_radius/half else model.outer_radius,
        let neutral = {*:model, inner_radius: 0.0, outer_radius: 1.0, direct_polar: true},
        let points = coordinate.line(neutral, [[a * model.width, (1.0 - inner) * model.height],
            [a * model.width, (1.0 - outer) * model.height], [b * model.width, (1.0 - outer) * model.height],
            [b * model.width, (1.0 - inner) * model.height]], true))
        if (not all([first,end_fraction,inner,outer] |> util.finite_number(~)) or first>end_fraction or inner<0 or inner>outer)
            error("chart: arc requires increasing finite angles and nonnegative ordered radii")
        else if (points is error) points else <path d: svg.line_path(points) ++ " Z",
            *:mark.style(ctx, row, options, {fill: color.category10[i % 10], opacity: 1.0, stroke: "white", 'stroke-width': 1}), mark.tooltip(ctx, row)>])
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
    let plot_children0 = [if ((spec.clip or theme.view_clip) and not coordinate.enabled(spec.coordinate)) clip_marks(marks_el, lay.plot_w, lay.plot_h) else marks_el];
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

    svg.svg_root(width, height, children, {*:cfg.svg_attributes(theme), *:interaction_attributes(spec)})
    }
}

fn interaction_attributes(spec) => if (spec._interactive != true) {} else {'data-chart-root': spec._view_path}

// ============================================================
// Faceted chart rendering (small multiples)
// ============================================================

fn render_faceted(spec) {
    let base_theme = cfg.resolve_theme(spec.config);
    let paints = paint.plan([base_theme.background, base_theme.title_color], spec._paint_scope);
    let theme = {*:base_theme, _paints: paints};
    let facet = spec.facet;
    let plan = spec._facet_plan;
    let columns = plan.columns;
    let n = len(spec._views);
    let spacing = if (facet.spacing != null) facet.spacing else 20;
    let headers = plan.headers;
    let images = [for (index, cell in spec._views)
        render_spec_scoped({*:cell, _viewport: spec._viewport}, spec._paint_scope ++ "-f" ++ string(index))];
    // Automatic cells may grow for guides; reserve the largest rendered extent in the grid.
    let sub_w = max([0.0, for (image in images where image is element) float(image.width)]);
    let sub_h = max([0.0, for (image in images where image is element) float(image.height)]);
    let flay = layout.compute_facet_layout(n, columns, sub_w, sub_h, spacing, spec.title, spec.padding, theme, headers);
    let failure = util.first_error([paints._error, flay._error, *images]);
    if (failure is error) failure else {
    let cells = [for (i in 0 to (n - 1),
        let pos = layout.facet_cell_pos(flay, i))
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
    let theme = cfg.resolve_theme(spec.config);
    let paints = paint.plan([theme.background], scope);
    let spacing = if (spec.spacing != null) float(spec.spacing) else 20.0;
    let subs = spec.children;
    let n = len(subs);
    let plan = if (not util.finite_number(spacing) or spacing < 0) error("chart: composition spacing must be finite and nonnegative")
        else sizing.composition(spec, subs);
    let fixed = if (plan is error) [] else [for (index, child in subs)
        if (plan[plan.key] != null and plan.flexible[index]) null
        else if (child is element and name(child) == 'svg') child
        else render_spec_scoped({*:child, _viewport: {width: plan.width, height: plan.height}}, scope ++ "-c" ++ string(index))];
    let fixed_error = util.first_error(fixed);
    let contexts = if (plan is error) plan else if (fixed_error is error) fixed_error else sizing.allocate(plan, fixed, spacing);
    let sub_svgs = if (contexts is error) [] else [for (index, child in subs)
        if (fixed[index] != null) fixed[index]
        else render_spec_scoped({*:child, _viewport: contexts[index]}, scope ++ "-c" ++ string(index))];
    let failure = if (contexts is error) contexts else util.first_error([paints._error, *sub_svgs]);
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
    let natural_w = if (is_h)
        max([0.0, for (p in items) p.x + p.w])
    else
        max([0.0, for (p in items) p.w]);
    let natural_h = if (is_h)
        max([0.0, for (p in items) p.h])
    else
        max([0.0, for (p in items) p.y + p.h]);
    let total_w = if (plan.width != null) plan.width else natural_w;
    let total_h = if (plan.height != null) plan.height else natural_h;

    // wrap each sub-svg in a translated group
    let groups = [for (i in 0 to (n - 1))
        <g transform: svg.translate(items[i].x, items[i].y),
            sub_svgs[i]
        >];

    let bg = <rect width: total_w, height: total_h, fill: paint.value(theme.background, paints)>;
    // Splitting fractional viewport sizes can accumulate a subpixel rounding difference.
    if (natural_w > total_w + 0.000001 or natural_h > total_h + 0.000001)
        error("chart: composition children exceed container dimensions")
    else svg.svg_root(total_w, total_h, [paint.definitions(paints), bg, *groups])
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
    let n_rows = len(row_fields);
    let n_cols = len(col_fields);
    let viewport = sizing.repeat_viewport(spec, n_rows, n_cols, 10.0);

    let sub_charts = if (viewport is error) [] else [for (index, child in spec._views)
        render_spec_scoped({*:child, _viewport: viewport}, scope ++ "-r" ++ string(index))];

    // build each row as an hconcat of its column charts
    let rows_svgs = [for (ri in 0 to (n_rows - 1),
                         let row_charts = [for (ci in 0 to (n_cols - 1)) sub_charts[ri * n_cols + ci]],
                         let row_spec = {concat: "horizontal", spacing: 10.0, children: row_charts, config: spec.config})
        render_concat(row_spec, scope ++ "-row" ++ string(ri))];

    if (viewport is error) viewport
    else if (n_rows == 1) rows_svgs[0]
    else render_concat({concat: "vertical", spacing: 10.0, children: rows_svgs, config: spec.config}, scope)
}

// Pure event reductions; only the chart view's on handlers assign the result (S12.1.3).
import parameter: .parameter
import parse: .parse
import expr: .expression
import scale: .scale
import svg: .svg
import util: .util
import streams: .event_stream

fn with_value(values, key, value) => {*:values, *:map([key, value])}
fn store(value, view_key, selected) => {*:value, stores: if (value.resolve == "global") map([view_key, selected])
    else with_value(value.stores, view_key, selected)}
fn set_selection(st, definition, view_key, selected) =>
    {*:st, values: with_value(st.values, definition.name, store(st.values[definition.name], view_key, selected))}

pub fn frame(spec, lay, x_scale = null, y_scale = null) => {
    view: spec._view_path, params: spec._parameters,
    encoding: map([for (key, channel in spec.encoding) for (part in [string(key), {field: channel.field, dtype: channel.dtype}]) part]),
    data: [for (row in spec.data) parse.attributes(row)], scales: {x: x_scale, y: y_scale},
    x: lay.plot_x, y: lay.plot_y, width: lay.plot_w, height: lay.plot_h,
    svg_width: lay.total_w, svg_height: lay.total_h
}

fn projected_fields(definition, frame) {
    let select = parameter.selection(definition);
    if (select.fields != null) select.fields
    else if (select.encodings != null) [for (key in select.encodings where frame.encoding[key].field != null) frame.encoding[key].field]
    else null
}
fn project(definition, frame, row) {
    let fields = projected_fields(definition, frame);
    if (fields == null) row else map([for (field in fields) for (part in [field, row[field]]) part])
}
fn axes(definition, frame) => [for (key in (if (parameter.selection(definition).encodings != null)
    parameter.selection(definition).encodings else ["x", "y"])
    where frame.encoding[key].field != null and frame.scales[key] != null) key]

fn stream_matches(stream, event) => streams.matches(stream, event, event._values)
fn start_matches(stream, event) => stream_matches(stream, event) or
    (event.type == "pointerdown" and stream_matches(stream, {*:event, type: "mousedown"}))

fn nearest(frame, event, encodings) {
    let keys = if (encodings != null) encodings else ["x", "y"];
    let candidates = [for (row in frame.data) (
        let distances = [for (key in keys where frame.encoding[key].field != null and frame.scales[key] != null and util.finite_number(event[key])) (
            let mapping = frame.scales[key],
            let coordinate = scale.scale_apply(mapping, row[frame.encoding[key].field]) + (if (mapping.bandwidth != null) mapping.bandwidth / 2.0 else 0.0),
            (coordinate - event[key]) ** 2.0)],
        {row: row, distance: if (len(distances) == 0) inf else sum(distances)})];
    let distance = min(candidates |> ~.distance);
    let found = candidates |: ~.distance == distance and ~.distance != inf;
    if (len(found) > 0) found[0].row else null
}

fn point(st, definition, event) {
    let select = parameter.selection(definition);
    let input_bound = definition.bind != null and definition.bind != "scales";
    let event_stream = if (select.on != null) select.on else if (input_bound) false else "click";
    let triggered = if (event.legend == true) definition.bind == "legend" and event.type == "click"
        else stream_matches(event_stream, event);
    let row = if (select.nearest == true and event.legend != true) nearest(event.frame, event, select.encodings) else event.row;
    if (not triggered or row == null) st else {
        let tuple = project(definition, event.frame, row);
        let selected = st.values[definition.name];
        let existing = if (selected.resolve == "global") [for (view_key, entries in selected.stores) for (entry in entries) entry]
            else if (selected.stores[event.frame.view] != null) selected.stores[event.frame.view] else [];
        let toggle = if (select.toggle is string) expr.test(expr.compile(select.toggle, {*:event._values, event: event}), null)
            else if (select.toggle == false) false else event.shiftKey == true;
        let present = contains(existing, tuple);
        let entries = if (not toggle) [tuple] else if (present) existing |: ~ != tuple else [*existing, tuple];
        set_selection(st, definition, event.frame.view, entries)
    }
}

fn brush_extent(mapping, begin, end) {
    let low = min([begin, end]);
    let high = max([begin, end]);
    if (mapping.kind == "band" or mapping.kind == "point" or mapping.kind == "ordinal")
        {kind: "discrete", values: [for (value in mapping.domain,
            let pixel = scale.scale_apply(mapping, value) + (if (mapping.bandwidth != null) mapping.bandwidth / 2.0 else 0.0)
            where pixel >= low and pixel <= high) value]}
    else sort([scale.scale_invert(mapping, low), scale.scale_invert(mapping, high)])
}
fn interval_extent(definition, frame, begin, end) => map([for (key in axes(definition, frame))
    for (part in [frame.encoding[key].field, brush_extent(frame.scales[key],
        util.clamp_val(begin[key], 0.0, frame[if (key == "x") "width" else "height"]),
        util.clamp_val(end[key], 0.0, frame[if (key == "x") "width" else "height"]))]) part])

fn current_extent(value, definition, frame) => map([for (key in axes(definition, frame),
    let field = frame.encoding[key].field,
    let selected = if (value.resolve == "global") parameter.extent(value, field) else value.stores[frame.view][field])
    for (part in [field, if (selected != null) selected else frame.scales[key].domain]) part])
fn inside(extent, definition, frame, event) => all([for (key in axes(definition, frame),
    let field = frame.encoding[key].field, let bounds = extent[field])
    if (bounds.kind == "discrete") contains(bounds.values, nearest(frame, event, [key])[field])
    else event[key] >= min([scale.scale_apply(frame.scales[key], bounds[0]), scale.scale_apply(frame.scales[key], bounds[1])]) and
         event[key] <= max([scale.scale_apply(frame.scales[key], bounds[0]), scale.scale_apply(frame.scales[key], bounds[1])])])

fn interval_start(st, definition, event) {
    let select = parameter.selection(definition);
    let lifecycle = streams.lifecycle(select.on);
    let starts = start_matches(lifecycle.start, event);
    let frame = event.frame;
    if (not starts or event.button != null and event.button != 0 or len(axes(definition, frame)) == 0) null else {
        let selected = st.values[definition.name];
        let extent = current_extent(selected, definition, frame);
        let translation = streams.lifecycle(if (select.translate == null or select.translate == true) select.on else select.translate);
        let translate = select.translate != false and start_matches(translation.start, event) and
            (definition.bind == "scales" or not parameter.empty(selected) and inside(extent, definition, frame, event));
        {definition: definition, frame: frame, start: {x: event.x, y: event.y}, extent: extent, translate: translate, lifecycle: if (translate) translation else lifecycle}
    }
}

fn shifted_axis(gesture, event, key, zoom) {
    let mapping = gesture.frame.scales[key];
    let bounds = gesture.extent[gesture.frame.encoding[key].field];
    let discrete = bounds.kind == "discrete";
    let pixels = [for (value in (if (discrete) bounds.values else bounds))
        scale.scale_apply(mapping, value) + (if (mapping.bandwidth != null) mapping.bandwidth / 2.0 else 0.0)];
    if (len(pixels) == 0) bounds else {
        let camera = gesture.definition.bind == "scales";
        let limit = gesture.frame[if (key == "x") "width" else "height"];
        let movement = if (zoom == null) (event[key] - gesture.start[key]) * (if (camera) -1.0 else 1.0) else 0.0;
        // brushes stop at the plot edges; a scale-bound camera can pan beyond them.
        let delta = if (camera) movement else util.clamp_val(movement, -min(pixels), limit - max(pixels));
        let shifted = [for (pixel in [min(pixels), max(pixels)])
            if (zoom == null) pixel + delta else event[key] + (pixel - event[key]) * zoom];
        if (camera) sort(shifted |> scale.scale_invert(mapping, ~))
        else brush_extent(mapping, util.clamp_val(shifted[0], 0.0, limit), util.clamp_val(shifted[1], 0.0, limit))
    }
}
fn shifted_extent(gesture, event, zoom = null) => map([for (key in axes(gesture.definition, gesture.frame))
    for (part in [gesture.frame.encoding[key].field, shifted_axis(gesture, event, key, zoom)]) part])

fn interval(st, definition, event) {
    let select = parameter.selection(definition);
    let zoom_stream = if (select.zoom == null or select.zoom == true) "wheel" else select.zoom;
    if (event.type != "wheel" or not stream_matches(zoom_stream, event) or
        (definition.bind != "scales" and parameter.empty(st.values[definition.name]))) st
    else {
        let gesture = {definition: definition, frame: event.frame,
            extent: current_extent(st.values[definition.name], definition, event.frame)};
        let factor = 1.001 ** util.clamp_val(if (event.deltaY != null) event.deltaY else 0.0, -1000.0, 1000.0);
        set_selection(st, definition, event.frame.view, shifted_extent(gesture, event, factor))
    }
}

fn update_parameters(st, definitions, event, index = 0) {
    if (index >= len(definitions)) st else {
        let definition = definitions[index];
        let select = parameter.selection(definition);
        let clear = if (select.clear != null) select.clear else if (definition.bind != null and definition.bind != "scales") false else "dblclick";
        let next = if (select == null) st
            else if (stream_matches(clear, event)) clear_selection(st, definition)
            else if (select.type == "point") point(st, definition, event)
            else interval(st, definition, event);
        update_parameters(next, definitions, event, index + 1)
    }
}

fn clear_selection(st, definition) {
    let remaining = st.gesture |: ~.definition.name != definition.name;
    {*:st, values: with_value(st.values, definition.name, {*:st.values[definition.name], stores: {}}),
        gesture: if (len(remaining) == 0) null else remaining}
}

// A normalized event carries data-space metadata from the rendered chart, never a DOM handle.
pub fn update(previous, raw_event) {
    let event = {*:raw_event, _values: parameter.expression_values(parameter.values(raw_event.frame.params, previous))};
    let st = if (previous is error or event.param != null) previous else update_parameters(previous, event.frame.params, event);
    if (st is error) st
    else if (event.param != null) {
        let found = event.frame.params |: ~.name == event.param;
        if (len(found) == 0) st else {
            let definition = found[0];
            if (definition.expr != null) st
            else if (definition.select == null) {*:st, values: with_value(st.values, event.param, event.value)}
            else (
                let tuples = [for (view_key, store in st.values[event.param].stores) for (tuple in store) tuple],
                set_selection(st, definition, event.frame.view,
                    [{*:parse.attributes(tuples[0]), *:map([event.field, event.value])}]))
        }
    } else if (st.gesture != null and any([for (gesture in st.gesture)
        stream_matches(gesture.lifecycle.move, event) or stream_matches(gesture.lifecycle.end, event)])) {
        let gestures = st.gesture;
        let updated = drag(st, gestures, event, 0);
        let remaining = gestures |: not stream_matches(~.lifecycle.end, event);
        {*:updated, gesture: if (len(remaining) == 0) null else remaining}
    } else if (event.type == "pointercancel" or event.type == "keydown" and event.key == "Escape") {*:st, gesture: null}
    else {
        let definitions = event.frame.params;
        let starts = if (event.type == "pointerdown") [for (definition in definitions
            where parameter.selection(definition).type == "interval") interval_start(st, definition, event)] |: ~ != null else [];
        if (len(starts) > 0) {*:st, gesture: starts} else st
    }
}
fn drag(st, gestures, event, index) {
    if (index >= len(gestures)) st else {
        let gesture = gestures[index];
        let extent = if (gesture.translate) shifted_extent(gesture, event)
            else interval_extent(gesture.definition, gesture.frame, gesture.start, event);
        let next = if (not stream_matches(gesture.lifecycle.move, event) and not stream_matches(gesture.lifecycle.end, event)) st else
            set_selection(st, gesture.definition, gesture.frame.view,
            if (event.x == gesture.start.x and event.y == gesture.start.y and not gesture.translate) {} else extent);
        drag(next, gestures, event, index + 1)
    }
}

fn overlay(frame, definition, st) {
    let value = st.values[definition.name];
    let selected = if (value.stores[frame.view] != null) value.stores[frame.view] else value.stores.initial;
    if (selected == null or len(selected) == 0 or definition.bind == "scales") null else {
        let bounds = [for (key in ["x", "y"], let field = frame.encoding[key].field,
            let extent = selected[field], let mapping = frame.scales[key])
            if (extent == null) [0.0, frame[if (key == "x") "width" else "height"]]
            else if (extent.kind == "discrete") (
                let pixels = [for (v in extent.values) scale.scale_apply(mapping, v)],
                [min(pixels), max(pixels) + (if (mapping.bandwidth != null) mapping.bandwidth else 0.0)])
            else sort([scale.scale_apply(mapping, extent[0]), scale.scale_apply(mapping, extent[1])])];
        let options = parameter.selection(definition).mark;
        if (len([for (pair in bounds) for (value in pair where not util.finite_number(value)) value]) > 0) null else
        <rect class: "chart-brush", 'data-chart-param': definition.name,
            x: bounds[0][0], y: bounds[1][0], width: bounds[0][1] - bounds[0][0], height: bounds[1][1] - bounds[1][0],
            fill: if (options.fill != null) options.fill else "#333", 'fill-opacity': if (options.fillOpacity != null) options.fillOpacity else 0.125,
            stroke: if (options.stroke != null) options.stroke else "white", 'pointer-events': "none">
    }
}

pub fn decorate(image, spec, lay, x_scale = null, y_scale = null, background = true) {
    if (image is error or spec._interactive != true) image else {
        let frame = frame(spec, lay, x_scale, y_scale);
        let invalid = [for (definition in spec._parameters where parameter.selection(definition).type == "interval")
            if (len(axes(definition, frame)) == 0) error("chart: interval selection requires positional field encodings")
            else if (definition.bind == "scales" and len([for (key in axes(definition, frame)
                where contains(["band", "point", "ordinal", "identity"], frame.scales[key].kind)) key]) > 0)
                error("chart: scale binding requires continuous positional scales")];
        let failure = util.first_error(invalid);
        if (failure is error) failure else <g 'data-chart-frame': format(frame, 'json'),
            if (background) <rect class: "chart-interaction-plane", width: lay.plot_w, height: lay.plot_h,
                fill: "transparent", 'pointer-events': "all">;
            image;
            for (definition in spec._parameters where parameter.selection(definition).type == "interval")
                overlay(frame, definition, spec._parameter_state)
        >
    }
}

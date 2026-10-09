// Pure event reductions; only the chart view's on handlers assign the result (S12.1.3).
import parameter: .parameter
import parse: .parse
import expr: .expression
import scale: .scale
import svg: .svg
import util: .util
import streams: .event_stream
import behavior: .behavior
import spatial: .coordinate
import picking: .picking
import projection:.projection

fn with_value(values, key, value) => {*:values, *:map([key, value])}
fn store(value, view_key, selected) => {*:value, stores: if (value.resolve == "global") map([view_key, selected])
    else with_value(value.stores, view_key, selected)}
fn set_selection(st, definition, view_key, selected) =>
    {*:st, values: with_value(st.values, definition.name, store(st.values[definition.name], view_key,
        if (definition._axis == true and len(selected) > 0) {*:parse.attributes(st.values[definition.name].stores[view_key]), *:selected} else selected))}

pub fn frame(spec, lay, x_scale = null, y_scale = null) => {
    view: spec._view_path, id: spec.id, qualified_id:spec._qualified_id, params: spec._parameters,
    behaviors: behavior.serializable(behavior.local(spec)), coordinate: spec._coordinate, axis_models: spec._axis_models,
    projection:if (contains(["geo","geoshape"],spec.mark.kind) or spec._coordinate.type=="geo")
        projection.configure(lay.plot_w,lay.plot_h,if (spec.projection!=null) spec.projection else spec._coordinate.projection) else null,
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
    parameter.selection(definition).encodings else if (definition._axis == true) parameter.selection(definition).fields else ["x", "y"])
    where frame.encoding[key].field != null and frame.scales[key] != null) key]

fn stream_matches(stream, event) => streams.matches(stream, event, event._values)
fn start_matches(stream, event) => stream_matches(stream, event) or
    (event.type == "pointerdown" and stream_matches(stream, {*:event, type: "mousedown"}))

fn nearest(frame, event, encodings) {
    let keys = if (encodings != null) encodings else ["x", "y"];
    let display = if (event._display != null) event._display else [event.x, event.y];
    let candidates = [for (row in frame.data) (
        let coordinates = [for (key in ["x", "y"]) if (frame.scales[key] != null)
            scale.scale_apply(frame.scales[key], row[frame.encoding[key].field]) +
                (if (frame.scales[key].bandwidth != null) frame.scales[key].bandwidth / 2.0 else 0.0) else null],
        let projected = if (frame.coordinate != null and all(coordinates |> util.finite_number(~))) spatial.pixel(frame.coordinate, coordinates) else coordinates,
        let distances = [for (index, key in ["x", "y"] where contains(keys, key) and
            util.finite_number(projected[index]) and util.finite_number(display[index])) (projected[index] - display[index]) ** 2.0],
        {row: row, distance: if (len(distances) == 0) inf else sum(distances)})];
    let distance = min(candidates |> ~.distance);
    let found = candidates |: ~.distance == distance and ~.distance != inf;
    if (len(frame.geometry)>0) picking.nearest(frame.geometry,display).row
    else if (len(found) > 0) found[0].row else null
}

fn point(st, definition, event) {
    let select = parameter.selection(definition);
    let input_bound = definition.bind != null and definition.bind != "scales";
    let event_stream = if (select.on != null) select.on else if (input_bound and definition.bind != "legend") false else "click";
    let triggered = if (event.legend == true) definition.bind == "legend" and stream_matches(event_stream, event)
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
fn axis_limit(frame, key) => if (key == "x") frame.width else if (key == "y") frame.height else max(frame.scales[key].range)
fn angular_axis(model) => if (contains(["theta", "radial"], model.type) and model.direct_polar != true) "y" else "x"
fn interval_extent(definition, frame, begin, end) => map([for (key in axes(definition, frame),
    let limit = axis_limit(frame, key),
    let bounds = brush_extent(frame.scales[key], util.clamp_val(begin[key], 0.0, limit), util.clamp_val(end[key], 0.0, limit)),
    let wrapped = contains(["polar", "theta", "radial"], frame.coordinate.type) and key == angular_axis(frame.coordinate) and
        abs(begin[key] - end[key]) > limit / 2.0)
    for (part in [frame.encoding[key].field, if (wrapped and bounds.kind == null) {kind: "wrapped", start: max(bounds), end: min(bounds)} else bounds]) part])

fn current_extent(value, definition, frame) => map([for (key in axes(definition, frame),
    let field = frame.encoding[key].field,
    let selected = if (value.resolve == "global") parameter.extent(value, field) else value.stores[frame.view][field])
    for (part in [field, if (selected != null) selected else frame.scales[key].domain]) part])
fn inside(extent, definition, frame, event) => all([for (key in axes(definition, frame),
    let field = frame.encoding[key].field, let bounds = extent[field])
    if (bounds.kind == "discrete") contains(bounds.values, nearest(frame, event, [key])[field])
    else if (bounds.kind == "wrapped") (
        let value=scale.scale_invert(frame.scales[key],event[key]),
        value>=bounds.start or value<=bounds.end)
    else event[key] >= min([scale.scale_apply(frame.scales[key], bounds[0]), scale.scale_apply(frame.scales[key], bounds[1])]) and
         event[key] <= max([scale.scale_apply(frame.scales[key], bounds[0]), scale.scale_apply(frame.scales[key], bounds[1])])])

fn interval_start(st, original, event) {
    let definition = if (original._axis == true) {*:original, select: {*:parameter.selection(original), fields: null, encodings: [event.axis]}} else original;
    let select = parameter.selection(definition);
    let lifecycle = streams.lifecycle(select.on);
    let starts = start_matches(lifecycle.start, event);
    let frame = event.frame;
    if (original._axis == true and event.axis == null) null else if (not starts or event.button != null and event.button != 0 or len(axes(definition, frame)) == 0) null else {
        let selected = st.values[definition.name];
        let extent = current_extent(selected, definition, frame);
        let translation = streams.lifecycle(if (select.translate == null or select.translate == true) select.on else select.translate);
        let translate = select.translate != false and start_matches(translation.start, event) and
            (definition.bind == "scales" or not parameter.empty(selected) and inside(extent, definition, frame, event));
        {definition: definition, frame: frame, start: event, extent: extent, translate: translate, lifecycle: if (translate) translation else lifecycle}
    }
}

fn shifted_axis(gesture, event, key, zoom) {
    let mapping = gesture.frame.scales[key];
    let bounds = gesture.extent[gesture.frame.encoding[key].field];
    let wrapped = bounds.kind == "wrapped";
    let discrete = bounds.kind == "discrete";
    if (wrapped) {
        let limit=axis_limit(gesture.frame,key);
        let start=scale.scale_apply(mapping,bounds.start);
        let end=scale.scale_apply(mapping,bounds.end);
        let a=if (zoom==null) start+event[key]-gesture.start[key] else event[key]+(start-event[key])*zoom;
        let b=if (zoom==null) end+event[key]-gesture.start[key] else a+((end-start+limit)%limit)*zoom;
        let aa=a-limit*floor(a/limit); let bb=b-limit*floor(b/limit);
        let values=[scale.scale_invert(mapping,aa),scale.scale_invert(mapping,bb)];
        if (values[0]>values[1]) {kind:"wrapped",start:values[0],end:values[1]} else values
    } else {
    let pixels = [for (value in (if (discrete) bounds.values else bounds))
        scale.scale_apply(mapping, value) + (if (mapping.bandwidth != null) mapping.bandwidth / 2.0 else 0.0)];
    if (len(pixels) == 0) bounds else {
        let camera = gesture.definition.bind == "scales";
        let limit = axis_limit(gesture.frame, key);
        let movement = if (zoom == null) (event[key] - gesture.start[key]) * (if (camera) -1.0 else 1.0) else 0.0;
        // brushes stop at the plot edges; a scale-bound camera can pan beyond them.
        let delta = if (camera) movement else util.clamp_val(movement, -min(pixels), limit - max(pixels));
        let shifted = [for (pixel in [min(pixels), max(pixels)])
            if (zoom == null) pixel + delta else event[key] + (pixel - event[key]) * zoom];
        if (camera) sort(shifted |> scale.scale_invert(mapping, ~))
        else brush_extent(mapping, util.clamp_val(shifted[0], 0.0, limit), util.clamp_val(shifted[1], 0.0, limit))
    }
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
fn update_core(previous, raw_event) {
    let event = {*:raw_event, _values: parameter.expression_values(parameter.values(raw_event.frame.params, previous))};
    let st = if (previous is error or event.param != null) previous else update_parameters(previous, event.frame.params, event);
    if (st is error) st
    else if (event.param != null) {
        if (event.field == null) parameter.update_value(st, event.frame.params, event.param, event.value, event.frame.view)
        else (
            let tuples = [for (owner, entries in st.values[event.param].stores) for (tuple in entries) tuple],
            parameter.update_value(st, event.frame.params, event.param,
                [{*:parse.attributes(tuples[0]), *:map([event.field, event.value])}], event.frame.view))
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

fn overlay_rect(frame, definition, st) {
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
        (let rect = <rect class: "chart-brush", 'data-chart-param': definition.name,
            x: bounds[0][0], y: bounds[1][0], width: bounds[0][1] - bounds[0][0], height: bounds[1][1] - bounds[1][0],
            fill: if (options.fill != null) options.fill else "#333", 'fill-opacity': if (options.fillOpacity != null) options.fillOpacity else 0.125,
            stroke: if (options.stroke != null) options.stroke else "white", 'pointer-events': "none">,
        if (frame.coordinate != null) spatial.warp(rect, frame.coordinate) else rect)
    }
}

fn overlay(frame, definition, st) {
    let value = st.values[definition.name];
    let selected = if (value.stores[frame.view] != null) value.stores[frame.view] else value.stores.initial;
    let wrapped = [for (field, bounds in selected where bounds.kind == "wrapped") {field: string(field), bounds: bounds}];
    if (definition._axis == true) <g class: "chart-axis-brushes", for (axis in frame.axis_models,
        let bounds = selected[axis.field] where bounds != null,
        let a = scale.scale_apply(axis.mapping, min(bounds)) / axis.length,
        let b = scale.scale_apply(axis.mapping, max(bounds)) / axis.length)
        <line class: "chart-axis-brush", x1: util.lerp(axis.a[0], axis.b[0], a), y1: util.lerp(axis.a[1], axis.b[1], a),
            x2: util.lerp(axis.a[0], axis.b[0], b), y2: util.lerp(axis.a[1], axis.b[1], b),
            stroke: "#4e79a7", 'stroke-width': 8, 'stroke-opacity': 0.3, 'pointer-events': "none">>
    else if (len(wrapped) == 0) overlay_rect(frame, definition, st)
    else <g class: "chart-wrapped-brush", for (entry in wrapped,
        let keys = [for (key in ["x", "y"] where frame.encoding[key].field == entry.field) key],
        let domain = frame.scales[keys[0]].domain)
        for (bounds in [[entry.bounds.start, max(domain)], [min(domain), entry.bounds.end]])
            overlay_rect(frame, definition, {*:st, values: {*:st.values, *:map([definition.name,
                {*:value, stores: map([frame.view, {*:selected, *:map([entry.field, bounds])}])}])}})>
}
fn crosshairs(frame, spec) => <g class: "chart-crosshairs", 'pointer-events': "none",
    for (item in spec._behaviors where item.type == "crosshair",
        let value = behavior.value(spec._parameter_state,item,spec._view_path) where value.display != null) (
        let inverse = if (frame.coordinate != null) spatial.invert(frame.coordinate, value.display) else null,
        let x = if (inverse is array) inverse[0] * frame.width else value.display[0],
        let y = if (inverse is array) (1.0 - inverse[1]) * frame.height else value.display[1],
        let guides = <g <line x1: x, y1: 0, x2: x, y2: frame.height, stroke: "#888", 'stroke-dasharray': "3,3">;
            <line x1: 0, y1: y, x2: frame.width, y2: y, stroke: "#888", 'stroke-dasharray': "3,3">>,
        if (frame.coordinate != null and not (inverse is error)) spatial.warp(guides, frame.coordinate) else guides)>

pub fn decorate(image, spec, lay, x_scale = null, y_scale = null, background = true) {
    if (image is error or spec._interactive != true) image else {
        let frame = {*:frame(spec, lay, x_scale, y_scale), geometry:picking.collect(image)};
        let invalid = [for (definition in spec._parameters where parameter.selection(definition).type == "interval")
            if (definition._axis!=true and frame.coordinate!=null and spatial.invert(frame.coordinate,[frame.width/2.0,frame.height/2.0]) is error)
                error("chart: interval selection requires an invertible coordinate")
            else if (definition._axis != true and len(axes(definition, frame)) == 0) error("chart: interval selection requires positional field encodings")
            else if (definition.bind == "scales" and len([for (key in axes(definition, frame)
                where contains(["band", "point", "ordinal", "identity"], frame.scales[key].kind)) key]) > 0)
                error("chart: scale binding requires continuous positional scales")];
        let failure = util.first_error(invalid);
        if (failure is error) failure else <g 'data-chart-frame': format(frame, 'json'),
            if (background) <rect class: "chart-interaction-plane", width: lay.plot_w, height: lay.plot_h,
                fill: "transparent", 'pointer-events': "all">;
            image;
            for (definition in spec._parameters where parameter.selection(definition).type == "interval")
                overlay(frame, definition, spec._parameter_state);
            crosshairs(frame, spec)
        >
    }
}

fn position(frame, event) {
    let model = frame.coordinate;
    let axis_models = if (frame.axis_models != null) frame.axis_models else [];
    let projected = [for (axis in axis_models, let dx = axis.b[0] - axis.a[0], let dy = axis.b[1] - axis.a[1],
        let t = ((event.x - axis.a[0]) * dx + (event.y - axis.a[1]) * dy) / (axis.length * axis.length))
        {field: axis.field, value: util.clamp_val(t, 0.0, 1.0) * axis.length}];
    let axis_frame = if (len(axis_models) == 0) frame else {*:frame,
        encoding: {*:frame.encoding, *:map([for (axis in axis_models) for (part in [axis.field, {field: axis.field}]) part])},
        scales: {*:frame.scales, *:map([for (axis in axis_models) for (part in [axis.field, axis.mapping]) part])}};
    let point = if (model != null and util.finite_number(event.x) and util.finite_number(event.y) and
        not contains(["parallel", "radar", "helix", "geo"], model.type)) spatial.invert(model, [event.x, event.y]) else null;
    if (point is error) point else if (point == null) {*:event, frame: axis_frame, *:map([for (axis in projected) for (part in [axis.field, axis.value]) part])} else {*:event,
        _display: [event.x, event.y], x: point[0] * frame.width, y: (1.0 - point[1]) * frame.height,
        data_position: map([for (key in ["x", "y"] where frame.encoding[key].field != null and frame.scales[key] != null)
            for (part in [frame.encoding[key].field, scale.scale_invert(frame.scales[key],
                if (key == "x") point[0] * frame.width else (1.0 - point[1]) * frame.height)]) part])}
}
fn cursor(st, event) {
    let items = event.frame.behaviors |: ~.type == "crosshair";
    if (len(items) == 0 or not contains(["pointermove", "mousemove", "pointerleave", "focusout"], event.type)) st else {
        let item = items[0];
        let displayed=if (event._display!=null) event._display else [event.x,event.y];
        let picked=if (item.nearest==true) picking.nearest(event.frame.geometry,displayed) else null;
        let points=[for (line in picked.lines) for (point in line.points) point];
        let value = if (contains(["pointerleave", "focusout"], event.type)) null else
            {display: if (len(points)>0) [avg(points |> ~[0]),avg(points |> ~[1])] else displayed, data: event.data_position,
                datum: if (picked!=null) picked.row else if (item.nearest == true) nearest(event.frame, event, item.encodings) else event.row};
        parameter.update_value(st, event.frame.params, item.param, value, event.frame.view)
    }
}
fn navigation(st,event) {
    let items=event.frame.behaviors |: ~._geo==true;
    let active=if (st.gesture[0].kind=="projection") st.gesture[0] else null;
    let item=if (active!=null) active.behavior else items[0];
    let lifecycle=streams.lifecycle(item.on);
    let model=if (active!=null) active.frame.projection else event.frame.projection;
    if (item==null) st else if (event.type=="pointercancel" or event.type=="keydown" and event.key=="Escape") {*:st,gesture:null}
    else if (active!=null and (stream_matches(active.lifecycle.move,event) or stream_matches(active.lifecycle.end,event))) {
        let value=projection.navigate(model,event.x-active.start.x,event.y-active.start.y);
        let next=parameter.update_value(st,event.frame.params,item.param,value,event.frame.view);
        if (value is error) value else {*:next,gesture:if (stream_matches(active.lifecycle.end,event)) null else st.gesture}
    } else if (start_matches(lifecycle.start,event) and (event.button==null or event.button==0) and item.translate!=false)
        {*:st,gesture:[{kind:"projection",definition:item.definition,behavior:item,frame:event.frame,start:event,lifecycle:lifecycle}]}
    else if (stream_matches(if (item.zoom==null or item.zoom==true) "wheel" else item.zoom,event) and item.zoom!=false) {
        let factor=1.001**util.clamp_val(if (event.deltaY!=null) event.deltaY else 0.0,-1000.0,1000.0);
        let value=projection.navigate(model,0.0,0.0,factor);
        if (value is error) value else parameter.update_value(st,event.frame.params,item.param,value,event.frame.view)
    } else st
}
// Native events and programmatic commands produce the same state and observational notices.
pub fn update(previous, raw_event) {
    let event = position(raw_event.frame, raw_event);
    let next = if (previous is error) previous else if (event is error) event
        else if (event.command != null) behavior.command(previous, event.frame, event)
        else if (any(event.frame.behaviors |> ~._geo==true) or previous.gesture[0].kind=="projection") navigation(previous,event)
        else cursor(update_core(previous, event), event);
    let result = if (next is error) next else behavior.custom(next, event);
    if (result is error) result else {*:result, _notifications: behavior.notifications(previous, result, event)}
}

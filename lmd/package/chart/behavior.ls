// Behavior presets compile into ordinary chart parameters; rendering remains pure (S12.1.3).
import parse: .parse
import parameter: .parameter
import util: .util
import streams: .event_stream
import composite:.composite

let presets = ["element_highlight", "element_select", "legend_highlight", "legend_filter", "brush_highlight",
    "brush_filter", "brush_axis_highlight", "brush_axis_filter", "pan_zoom", "crosshair", "tooltip"]
fn part_names(kind) => if (contains(["sankey", "chord", "force_graph", "tree"], kind)) ["node", "link", "label"]
    else if (kind == "pack") ["node", "label"] else if (kind == "funnel") ["stage", "connector", "label"]
    else if (kind == "gauge") ["value", "track", "pointer", "target", "label"]
    else if (kind == "liquid") ["value", "track", "label"] else []
pub fn merge(parent, own) => {*:parse.attributes(parent), *:parse.attributes(own)}
fn interval(kind: string) => starts_with(kind, "brush") or kind == "pan_zoom"
fn highlight(kind: string) => ends_with(kind, "highlight")
fn legend(kind: string) => starts_with(kind, "legend")
fn projection(options, encoding, kind) {
    if (options.fields != null) {fields: options.fields}
    else if (options.encodings != null) {encodings: options.encodings}
    else if (options.group_by != null) {fields: [for (key in options.group_by)
        if (encoding[key].field != null) encoding[key].field else key]}
    else if (legend(kind)) {fields: [encoding.color.field]}
    else if (starts_with(kind, "brush_axis")) {fields: [for (entry in (if (encoding.position is array) encoding.position else encoding.position.fields))
        if (entry is string) entry else entry.field], axis: true}
    else if (interval(kind)) {encodings: [for (key in ["x", "y"] where encoding[key].field != null) key]}
    else if (encoding.key.field != null) {fields: [encoding.key.field]}
    else {}
}

fn compile_one(kind, raw, encoding, path, definitions, camera=null) {
    let options = if (raw == true) {} else parse.attributes(raw);
    let custom = options.handler is fn;
    let id = if (options.id != null) options.id else kind;
    let param = if (options.param != null) options.param else "chart_" ++ replace(path, "-", "_") ++ "_" ++ id;
    let matches = definitions |: ~.name == param;
    let existing = matches[0];
    let geographic=kind=="pan_zoom" and camera!=null;
    let selection_type = if (contains(["crosshair", "tooltip"], kind) or custom or geographic) null else if (interval(kind)) "interval" else "point";
    let projected = if (existing != null and options.fields == null and options.encodings == null and options.group_by == null)
        {fields: parameter.selection(existing).fields, encodings: parameter.selection(existing).encodings}
        else projection(options, encoding, kind);
    let on = if (options.on != null) options.on else if (highlight(kind) and not interval(kind)) "pointerover, focusin"
        else if (selection_type == "point") "click, keydown[event.key == 'Enter' || event.key == ' ']" else null;
    let clear = if (options.clear != null) options.clear else if (highlight(kind) and not interval(kind)) "pointerout, focusout" else "dblclick";
    let select = {*:parse.attributes(parameter.selection(existing)), type: selection_type, *:projected,
        on: on, clear: clear, toggle: if (options.toggle != null) options.toggle else "event.shiftKey",
        resolve: if (options.resolve != null) options.resolve else if (existing!=null) parameter.selection(existing).resolve else "union",
        translate: options.translate, zoom: options.zoom};
    let definition = {*:parse.attributes(existing), name: param, select: if (selection_type != null) select else null,
        bind: if (kind == "pan_zoom" and not geographic) "scales" else if (legend(kind)) "legend" else existing.bind,
        value:if (geographic and existing==null) {center:[0.0,0.0],*:parse.attributes(camera)} else existing.value,
        _encoding: encoding, _behavior: true, _scoped:options.param==null, _axis: projected.axis == true, _projection:geographic};
    let failure = util.first_error([
        if (not contains(presets, kind) and not custom) error("chart: unknown behavior " ++ kind),
        if (raw != true and not (raw is map or raw is element)) error("chart: behavior options must be true, false or a map"),
        if (options.param != null and len(matches) != 1) error("chart: behavior parameter must name a declared parameter"),
        if (existing != null and parameter.selection(existing).type != selection_type) error("chart: behavior parameter type mismatch"),
        if (existing != null and options.group_by != null and parameter.selection(existing).fields != projected.fields)
            error("chart: behavior parameter projection mismatch"),
        if (existing != null and options.fields != null and parameter.selection(existing).fields != options.fields)
            error("chart: behavior parameter projection mismatch"),
        if (existing != null and options.encodings != null and parameter.selection(existing).encodings != options.encodings)
            error("chart: behavior parameter projection mismatch"),
        if (legend(kind) and (len(projected.fields) != 1 or projected.fields[0] == null)) error("chart: legend behavior requires one projected field"),
        if (starts_with(kind, "brush_axis") and len(projected.fields) < 2) error("chart: axis brushing requires position fields"),
        streams.validate(on), streams.validate(clear)]);
    if (failure is error) failure else {*:options, type: kind, id: id, param: param, definition: definition,
        generated: options.param == null, scope: path, on: on, clear: clear, _geo:geographic}
}

fn compile_node(raw, inherited, encoding, path, definitions, states = {}, spatial=null, projection_options=null) {
    if (raw is element and name(raw)=='svg') raw else {
    let spec = if (raw is element) parse.parse_top(raw) else raw;
    let enc = merge(encoding, spec.encoding);
    let options = merge(merge(inherited, spec.interaction), spec.mark.interaction);
    let styles = merge(states, merge(spec.state, spec.mark.state));
    let coordinate=if (spec.coordinate!=null) spec.coordinate else if (spec.mark.coordinate!=null) spec.mark.coordinate else spatial;
    let projection=if (spec.mark.projection!=null) spec.mark.projection else if (spec.projection!=null) spec.projection
        else if (coordinate.projection!=null) coordinate.projection else projection_options;
    let camera=if (coordinate=="geo" or coordinate.type=="geo" or contains(["geo","geoshape"],spec.mark.kind))
        if (projection is string) {type:projection} else parse.attributes(projection) else null;
    let has_children = spec.children != null or spec.layer != null or spec.template != null or spec._factory_catalogue!=null;
    let behaviors = if (has_children) [] else [for (key, value in options where value != false and value != null)
        compile_one(string(key), value, enc, path, definitions,camera)];
    let parts = if (has_children) {} else map([for (part in util.unique_vals([*part_names(spec.mark.kind),
        for (key, value in spec.mark.parts) string(key)])) for (value in [part,
            compile_node({*:parse.attributes(spec.mark.parts[part]), part: part}, options, enc,
                path ++ "-p" ++ part, definitions, styles,coordinate,projection)]) value]);
    let failure = util.first_error([*behaviors, for (key, value in parts) value,
        if (len(util.unique_vals(behaviors |> ~.id))!=len(behaviors) or any(behaviors |> not (~.id is string)))
            error("chart: behavior ids must be unique strings within a mark or part")]);
    if (failure is error) failure else {*:spec, encoding: enc, _behaviors: behaviors,
        params: [*spec.params, for (item in behaviors where item.generated) item.definition,
            for (key, part in parts) for (definition in part.params) definition], _parts: parts,
        children: if (spec._factory_catalogue!=null) spec.children else if (spec.children != null) [for (i, child in spec.children) compile_node(child, options, enc, path ++ "-c" ++ string(i), definitions, styles,coordinate,projection)] else null,
        layer: if (spec._factory_catalogue!=null) spec.layer else if (spec.layer != null) [for (i, child in spec.layer) compile_node(child, options, enc, path ++ "-l" ++ string(i), definitions, styles,coordinate,projection)] else null,
        _factory_catalogue:if (spec._factory_catalogue!=null) [for (i,child in spec._factory_catalogue)
            compile_node(child,options,enc,path++"-cell"++util.binding_key(child._cell_key),definitions,styles,coordinate,projection)] else null,
        template: if (spec.template != null and spec._factory_catalogue==null) compile_node(spec.template, options, enc, path ++ "-template", definitions, styles,coordinate,projection) else null,
        timeline:if (spec.timeline!=null) {*:spec.timeline,keyframes:[for (i,frame in spec.timeline.keyframes,
            let child=if (frame.spec is element) parse.parse_top(frame.spec) else frame.spec)
            {*:frame,spec:compile_node({*:spec,*:child,params:child.params,timeline:null},options,enc,path++"-k"++string(i),definitions,styles,coordinate,projection)}]} else null,
        state: styles, _behavior_scope: path, _tooltip_disabled: options.tooltip == false}
    }
}

pub fn collect(spec) => [*spec._behaviors, for (child in [*spec.children, *spec.layer, spec.template,
    for (key, part in spec._parts) part,for (frame in spec.timeline.keyframes) frame.spec,*spec._factory_catalogue] where child != null)
    for (item in collect(child)) item]
pub fn local(spec) => [*spec._behaviors, for (key, part in spec._parts)
    for (item in part._behaviors) {*:item, part: string(key)}]
fn errors(spec) => if (spec is error) spec else util.first_error([for (child in [*spec.children, *spec.layer, spec.template,for (frame in spec.timeline.keyframes) frame.spec,*spec._factory_catalogue] where child != null) errors(child)])
fn view_ids(spec) => [if (spec.id!=null) spec.id,for (child in [*spec.children,*spec.layer,spec.template] where child!=null)
    for (id in view_ids(child)) id] |: ~!=null
pub fn prepare(spec,st=null) {
    if (spec._behavior_compiled == true or spec is element and name(spec) == 'svg') spec else {
        let declared=parameter.definitions(spec);
        let initial=if (st!=null) st else parameter.initial(declared);
        let expanded=if (composite.present(spec)) composite.materialize(spec,parameter.values(declared,initial)) else spec;
        let compiled = if (expanded is error) expanded else compile_node(expanded, {}, {}, "root", parameter.definitions(expanded));
        let ids=view_ids(compiled);
        let failure = util.first_error([errors(compiled),if (len(util.unique_vals(ids))!=len(ids) or any(ids |> not (~ is string)))
            error("chart: authored view ids must be unique strings")]);
        let definitions = parameter.definitions(compiled);
        if (failure is error) failure else {*:compiled, _behavior_compiled: true, _compiled_definitions: definitions, _all_behaviors: collect(compiled)}
    }
}

pub fn definitions(spec, inherited) {
    let overrides = local(spec) |> ~.definition;
    // A shared parameter is reduced once even if multiple effects refer to it.
    [for (definition in inherited, let found = overrides |: ~.name == definition.name
        where definition._behavior != true or len(found) > 0) (
        if (len(found) > 0) {*:found[0], select: if (parameter.selection(found[0]) == null) null else {*:parse.attributes(parameter.selection(found[0])),
            on: [for (item in found) parameter.selection(item).on],
            clear: [for (item in found) parameter.selection(item).clear]}} else definition)]
}
fn applies(item, spec) bool | error => if (item.targets != null) contains(item.targets, spec.id) or contains(item.targets, spec._view_path) or contains(item.targets,spec._qualified_id)
    else item.scope == spec._behavior_scope or contains(spec._behaviors |> ~.param, item.param)
pub fn value(st,item,view_key) {
    let current=st.values[item.param];
    if (item.generated!=true or view_key==null) current else if (current.stores!=null) {*:current,stores:map([view_key,current.stores[view_key]])}
    else if (current.views!=null and parse.has_attribute(current.views,view_key)) current.views[view_key] else current
}
pub fn filter_data(spec, data) => [for (row in data where all([for (item in spec._all_behaviors
    where ends_with(item.type, "filter") and applies(item, spec))
        parameter.selected(value(spec._parameter_state,item,spec._view_path), row, item.empty != false)])) row]
pub fn retain_domains(spec) => any([for (item in spec._all_behaviors where ends_with(item.type, "filter") and applies(item, spec)) item.rescale != true])

fn related(item, row, selected) {
    if (parameter.selected(selected, row, false)) true
    else if (item.relationship == null) false
    else any([for (field in [item.relationship.source, item.relationship.target] where field != null)
        parameter.selected(selected, map([if (item.relationship.node != null) item.relationship.node else "id", row[field]]), false)])
}
pub fn flags(ctx, row) {
    let active = [for (item in ctx._behaviors,let current=value(ctx._parameter_state,item,ctx._view_path) where highlight(item.type) and not parameter.empty(current))
        related(item, row, current)];
    let selected = [for (item in ctx._behaviors,let current=value(ctx._parameter_state,item,ctx._view_path) where item.type == "element_select" and not parameter.empty(current))
        related(item, row, current)];
    {active: any(active), inactive: len(active) > 0 and not any(active),
        selected: any(selected), unselected: len(selected) > 0 and not any(selected)}
}
pub fn styles(ctx, row, options) {
    let states = merge(ctx._state_styles, options.state);
    let flags = {*:flags(ctx, row), default: true};
    map([for (key in util.unique_vals([for (state_name in ["default", "inactive", "active", "unselected", "selected"] where flags[state_name])
        for (key, value in states[state_name]) string(key)]),
        let values = [for (state_name in ["default", "inactive", "active", "unselected", "selected"] where flags[state_name] and parse.has_attribute(states[state_name], key)) states[state_name][key]])
        for (part in [replace(key, "_", "-"), values[len(values) - 1]]) part])
}

pub fn tooltip(ctx, row) {
    let items = ctx._behaviors |: ~.type == "tooltip";
    let options = items[0];
    if (ctx._tooltip_disabled == true) false
    else if (options.format is fn) options.format(row)
    else if (options.fields != null) join([for (field in options.fields) field ++ ": " ++ string(row[field])], "\n")
    else null
}

// Every renderer receives the same behavior and identity context, including specialized marks.
pub fn context(spec) => {_behaviors: spec._behaviors, _parameter_state: spec._parameter_state,
    _parameter_values: if (spec._timing_values != null) spec._timing_values else spec._parameter_values, _state_styles: spec.state, _tooltip_disabled: spec._tooltip_disabled, _animate: spec.animate, _time: spec._time,
    _part: spec.part, _parts: spec._parts, _requires_key:spec._requires_key,_density:spec._density,_density_contours:spec._density_contours, _parameters: spec._parameters, _navigation:any(spec._behaviors |> ~._geo==true),
    _view_id: if (spec.id != null) spec.id else spec._view_path}
pub fn camera_options(spec,parent=null) {
    let navigators=spec._behaviors |: ~._geo==true;
    let own=if (spec.projection!=null) spec.projection else parent;
    if (len(navigators)==0) own else {
        let current=value(spec._parameter_state,navigators[0],spec._view_path);
        {*:if (own is string) {type:own} else parse.attributes(own),*:parse.attributes(current)}
    }
}

pub fn serializable(items) => [for (item in items) map([for (key, value in item
    where not contains(["handler", "definition"], string(key))) for (part in [string(key), value]) part])]
fn bind_guide_channel(channel,spec) => if (channel is array) [for (entry in channel) bind_guide_channel(entry,spec)]
    else if (channel._interaction!=null) {*:channel,_interaction:{*:channel._interaction,id:spec.id,behaviors:serializable(local(spec))}}
    else channel
pub fn bind_guides(encoding,spec) => map([for (key,channel in encoding) for (part in [string(key),bind_guide_channel(channel,spec)]) part])

pub fn command(st, frame, action) {
    let matches = frame.behaviors |: ~.id == action.id or ~.param == action.param;
    let item = matches[0];
    let key = if (action.param != null) action.param else item.param;
    if (key == null) error("chart: action requires a behavior id or parameter")
    else parameter.update_value(st, frame.params, key, action.value, frame.view, action.command)
}

fn custom_updates(st, frame, updates, index = 0) {
    let keys = [for (key, value in updates) string(key)];
    if (st is error or index >= len(keys)) st else custom_updates(
        parameter.update_value(st, frame.params, keys[index], updates[keys[index]], frame.view), frame, updates, index + 1)
}
pub fn custom(st, event, index = 0, notifications = []) {
    let items = event.frame.behaviors |: ~.handler is fn;
    if (st is error) st else if (index >= len(items)) {*:st, _custom_notifications: notifications} else {
        let item = items[index];
        let triggered = item.on == null or streams.matches(item.on, event);
        let result = if (triggered) item.handler(event, event.frame.data,
            parameter.expression_values(parameter.values(event.frame.params, st)), event.frame.coordinate) else {};
        let invalid = util.first_error([if (not (result is map)) error("chart: custom behavior must return updates and notifications"),
            for (notice in result.notifications) if (not (notice.name is string)) error("chart: custom notification requires a name")]);
        if (result is error) result else if (invalid is error) invalid
        else custom(custom_updates(st, event.frame, result.updates), event, index + 1, [*notifications, *result.notifications])
    }
}

pub fn notifications(before, after, event) => [for (item in event.frame.behaviors,
    let previous = before.values[item.param], let current = after.values[item.param],
    let starting = any([for (gesture in after.gesture) gesture.definition.name == item.param]) and
        not any([for (gesture in before.gesture) gesture.definition.name == item.param]),
    let ending = any([for (gesture in before.gesture) gesture.definition.name == item.param]) and
        not any([for (gesture in after.gesture) gesture.definition.name == item.param])
    where previous != current or starting or ending)
    {id: item.id, type: item.type, phase: if (starting) "start" else if (ending) "end"
        else if (parameter.empty(current) and current.kind != null) "clear" else "update",
        view: if (event.frame.id != null) event.frame.id else event.frame.view, view_path:event.frame.view,qualified_view:event.frame.qualified_id,
        key: event.element_key, part: event.part, datum: event.row, param: item.param,
        previous: previous, current: current, origin: event.type,
        position: {display: [event.x, event.y], data: event.data_position}}]

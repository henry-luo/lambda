// Declarative parameters are values owned by a view instance (S9.1.4, D6.2.3v2).
import expr: .expression
import parse: .parse
import util: .util
import calendar: .calendar
import streams: .event_stream

pub fn selection(definition) => if (definition.select is string) {type: definition.select} else definition.select
pub fn definitions(spec, inherited_encoding = null) {
    if (spec._compiled_definitions != null) spec._compiled_definitions else {
    let node = if (spec is element) parse.parse_top(spec) else spec;
    let encoding = {*:parse.attributes(inherited_encoding), *:parse.attributes(node.encoding)};
    [*[for (definition in node.params) {*:parse.attributes(definition), _encoding: if (definition._encoding != null) definition._encoding else encoding}],
        for (child in [*(if (node.children != null and node._factory_catalogue==null) node.children else []),
            *(if (node.layer != null and node._factory_catalogue==null) node.layer else []), if (node.template != null and node._factory_catalogue==null) node.template,
            for (frame in node.timeline.keyframes) frame.spec,*node._factory_catalogue] where child != null)
            for (definition in definitions(child, encoding)) definition]
    }
}

pub fn validate(definitions) {
    let names = definitions |> ~.name;
    let bindings = map([for (definition in definitions) for (part in [definition.name, definition.value]) part]);
    util.first_error([if (len(util.unique_vals(names)) != len(names)) error("chart: parameter names must be unique"),
        for (definition in definitions, let select = selection(definition))
            if (not expr.binding_name(definition.name))
                error("chart: parameter requires a nonreserved name")
            else if (select != null and not contains(["point", "interval"], select.type))
                error("chart: selection type must be point or interval")
            else if (select.resolve != null and not contains(["global", "union", "intersect"], select.resolve))
                error("chart: selection resolve must be global, union or intersect")
            else if (select.type == "interval" and select.fields != null and definition._axis != true)
                error("chart: interval selection projects encodings, not fields")
            else if (definition.bind == "scales" and select.type != "interval")
                error("chart: scale binding requires an interval selection")
            else if (definition.bind == "legend" and select.type != "point")
                error("chart: legend binding requires a point selection")
            else if (definition.bind == "legend" and len(if (select.fields != null) select.fields else select.encodings) != 1)
                error("chart: legend binding requires exactly one projected field or encoding")
            else if (definition.bind is string and not contains(["legend", "scales"], definition.bind))
                error("chart: unknown parameter binding")
            else if (definition.bind is map and select != null and select.type != "point")
                error("chart: input binding requires a variable or point selection")
            else if (definition.bind is map and select.type == "point" and len(if (select.fields != null) select.fields else select.encodings) == 0)
                error("chart: input-bound point selection requires a projection")
            else if (select.fields != null and select.encodings != null)
                error("chart: selection projects fields or encodings, not both")
            else if (select.type == "point" and definition.value != null and not (definition.value is array))
                error("chart: initial point selection must be an array of projected tuples")
            else if (select.type == "point" and any([for (tuple in definition.value) not (tuple is map or tuple is element)]))
                error("chart: initial point values must be projected records")
            else if (select.type == "interval" and definition.value != null and
                (not (definition.value is map) or any([for (field, bounds in definition.value)
                    not (bounds is array) or len(bounds) != 2 or any([for (bound in bounds) initial_bound(bound) is error])])))
                error("chart: initial interval values require two numeric or temporal bounds")
            else if (select.type == "interval" and select.encodings != null and
                len(select.encodings |: not contains(["x", "y"], ~)) > 0)
                error("chart: interval selection supports x and y encodings")
            else util.first_error([validate_binding(definition.bind), streams.validate(select.on, bindings), streams.validate(select.clear, bindings),
                streams.validate(select.translate, bindings), streams.validate(select.zoom, bindings),
                if (select.toggle is string) expr.compile(select.toggle, {*:bindings, event: {}})])])
}

fn initial_bound(value) => if (util.finite_number(value)) value else calendar.timestamp(value)

fn validate_binding(binding) {
    if (not (binding is map)) null
    else if (binding.element != null) error("chart: external input bindings are not supported")
    else if (binding.input != null and not contains(["text", "number", "range", "checkbox", "select", "radio"], binding.input))
        error("chart: unsupported parameter input type")
    else if (contains(["select", "radio"], binding.input) and not (binding.options is array))
        error("chart: option input binding requires an options array")
    else if (binding.input == null) util.first_error([for (key, value in binding) validate_binding(value)])
    else null
}

pub fn initial(definitions) {
    let failure = validate(definitions);
    if (failure is error) failure else {gesture: null, values: map([for (definition in definitions,
        let select = selection(definition)) for (part in [definition.name,
            if (select == null) definition.value
            else {kind: select.type, resolve: if (select.resolve != null) select.resolve else "global",
                stores: if (definition.value == null) {} else {initial: if (select.type != "interval") definition.value
                    else map([for (key, extent in definition.value) for (part in [
                        if (definition._encoding[key].field != null) definition._encoding[key].field else string(key),
                        [for (bound in extent) initial_bound(bound)]]) part])}}]) part])}
}
pub fn reconcile(definitions,st) {
    let defaults=initial(definitions);
    if (defaults is error) defaults else if (st==null) defaults else {*:st,values:{*:defaults.values,
        *:map([for (key,value in st.values where contains(definitions |> ~.name,string(key))) for (part in [string(key),value]) part])}}
}

fn computed(definitions, values, pending, remaining) {
    if (len(pending) == 0) values
    else if (remaining == 0) error("chart: parameter expressions contain an unknown name or dependency cycle")
    else {
        let results = [for (definition in pending) {definition: definition,
            value: expr.evaluate(expr.compile(definition.expr, expression_values(values)), null)}];
        let next = {*:values, *:map([for (result in results where not (result.value is error))
            for (part in [result.definition.name, result.value]) part])};
        computed(definitions, next, [for (result in results where result.value is error) result.definition], remaining - 1)
    }
}

pub fn values(definitions, st) {
    let base = map([for (definition in definitions where definition.expr == null)
        for (part in [definition.name, st.values[definition.name]]) part]);
    computed(definitions, base, definitions |: ~.expr != null, len(definitions))
}

fn tuple_matches(tuple, row) => all([for (field, value in tuple)
    if (row[field] is datetime or value is datetime) calendar.timestamp(row[field]) == calendar.timestamp(value)
    else row[field] == value])
fn store_matches(value, store, row) => if (value.kind == "point") any([for (tuple in store) tuple_matches(tuple, row)])
    else all([for (field, extent in store, let actual = if (extent.kind == "discrete" or row[field] is number) row[field]
        else calendar.timestamp(row[field])) row[field] != null and not (actual is error) and
        (if (extent.kind == "discrete") contains(extent.values, row[field])
         else if (extent.kind == "wrapped") actual >= extent.start or actual <= extent.end
         else actual >= min(extent) and actual <= max(extent))])
pub fn empty(value) => value == null or len([for (view_key, store in value.stores where len(store) > 0) store]) == 0

pub fn selected(value, row, empty_matches = true) {
    if (value == null or value.kind == null) error("chart: predicate parameter must name a selection")
    else if (empty(value)) empty_matches
    else {
        let tests = [for (view_key, store in value.stores where len(store) > 0) store_matches(value, store, row)];
        if (value.resolve == "intersect") all(tests) else any(tests)
    }
}

pub fn predicate(test, values) {
    if (test is string) {
        let compiled = expr.compile(test, expression_values(values));
        if (compiled is error) compiled else {_expression: compiled}
    } else if (test.param != null) {
        let value = values[test.param];
        if (not parse.has_attribute(values, test.param)) error("chart: unknown parameter " ++ string(test.param))
        else {*:parse.attributes(test), test: (row) =>
            if (value.stores != null and contains(["point", "interval"], value.kind)) selected(value, row, test.empty != false)
            else expr.truthy(value)}
    } else if (test is map or test is element) {
        let entries = [for (key, value in parse.attributes(test)) {key: string(key), value:
            if (contains(["test", "not"], string(key))) predicate(value, values)
            else if (contains(["and", "or"], string(key))) [for (part in value) predicate(part, values)]
            else value}];
        let failure = util.first_error([for (entry in entries)
            if (entry.value is array) util.first_error(entry.value) else entry.value]);
        if (failure is error) failure else map([for (entry in entries) for (part in [entry.key, entry.value]) part])
    } else test
}

// Parameter scale domains are derived from the stored data-space extent.
pub fn extent(value, field) {
    let extents = [for (view_key, store in value.stores where store[field] != null) store[field]];
    if (len(extents) == 0) null
    else if (extents[0].kind == "discrete") util.unique_vals([for (item in extents) for (v in item.values) v])
    else if (any(extents |> ~.kind=="wrapped")) if (len(extents)==1) extents[0]
        else {kind:"resolved",resolve:value.resolve,extents:extents}
    else if (value.resolve == "intersect") [max(extents |> min(~)), min(extents |> max(~))]
    else [min(extents |> min(~)), max(extents |> max(~))]
}

// Expressions see Vega-Lite's projected values, while predicates retain resolved stores.
pub fn expression_values(values) => map([for (key, value in values) for (part in [string(key),
    if (value.kind == "interval" and value.stores != null) map([for (field in util.unique_vals([for (view_key, store in value.stores)
        for (field, extent in store) string(field)])) for (item in [field, extent(value, field)]) item])
    else if (value.kind == "point" and value.stores != null) (
        let tuples = [for (view_key, store in value.stores) for (tuple in store) tuple],
        let fields = util.unique_vals([for (tuple in tuples) for (field, item in tuple) string(field)]),
        {*:map([for (field in fields) for (item in [field, util.unique_vals(tuples |> ~[field])]) item]), vlPoint: {'or': tuples}})
    else value]) part])

fn option(value, values, encoding) {
    if (value is array) [for (entry in value) option(entry, values, encoding)]
    else if (value.expr != null) expr.evaluate(expr.compile(value.expr, expression_values(values)), null)
    else if (value.param != null) {
        let selected = values[value.param];
        let field = if (value.field != null) value.field else encoding[value.encoding].field;
        if (selected.kind != "interval") error("chart: scale domain parameter must name an interval selection")
        else extent(selected, field)
    } else if (value is map) map([for (key, entry in value)
        for (part in [string(key), option(entry, values, encoding)]) part])
    else value
}

pub fn encoding(encoding, values, definitions, st, view_key, interactive) {
    map([for (key, channel in encoding)
        for (part in [string(key), if (channel == null) null else if (channel is array)
            [for (entry in channel) bind_channel(entry, values, encoding, definitions, st, view_key, interactive, string(key))]
            else bind_channel(channel, values, encoding, definitions, st, view_key, interactive, string(key))]) part])
}

fn bind_channel(raw, values, encoding, definitions, st, view_key, interactive, key) {
    let channel = if (key=="position" and raw is string) {field:raw,dtype:"quantitative"} else raw;
    let bound = map([for (key, value in parse.attributes(channel)) for (part in [string(key),
        if (string(key) == "condition") (if (value is array) [for (condition in value) predicate(condition, values)] else predicate(value, values))
        // Internal event metadata already contains bound parameter definitions.
        else if (starts_with(string(key),"_")) value
        else option(value, values, encoding)]) part]);
    let scaled = [for (definition in definitions,
        let selected = st.values[definition.name],
        let local = selected.stores[view_key],
        let domain = if (selected.resolve == "global") extent(selected, channel.field)
            else if (local != null) local[channel.field] else null
        where definition.bind == "scales" and contains(["x", "y"], key) and domain != null) domain];
    {*:bound, *:(if (len(scaled) > 0) {scale: {*:parse.attributes(bound.scale), domain: scaled[0], nice: false, zero: false}} else {}),
        *:(if (interactive) {_interaction: {view: view_key, field: channel.field, params: definitions}} else {})}
}

pub fn transforms(steps, values) {
    [for (step in (if (steps is element) content(steps) else steps),
        let tag = if (step is element) string(name(step)) else step.type)
        if (tag == "filter") {*:parse.attributes(step), type: tag, test: predicate(if (step.test != null) step.test else step, values)}
        else if (tag == "calculate" and step.expression is string) (
            let compiled = expr.compile(step.expression, expression_values(values)),
            // converting an element to a bound map must retain its transform tag.
            if (compiled is error) compiled else {*:parse.attributes(step), type: tag, expression: (row) => expr.evaluate(compiled, row)})
        else step]
}

pub fn target_attributes(ctx, row) => if (ctx._interactive != true and ctx._animate == null and ctx._requires_key!=true) {} else
    {'data-chart-row': format(parse.attributes(row), 'json'), 'data-chart-view': ctx._view_path,
        *:(if (ctx._part != null) {'data-chart-part': ctx._part} else {}),
        *:(if (ctx.encoding.key != null) {'data-chart-key': format(parse.channel_value(ctx.encoding.key,row), 'json'),
            'data-focus-key': format([ctx._view_path, ctx._part, parse.channel_value(ctx.encoding.key,row)], 'json')} else {}),
        *:(if (ctx._interactive == true and len(ctx._behaviors) > 0) {tabindex: "0"} else {})}

// Commands, controls and custom behaviors share one validated write boundary.
pub fn update_value(st, definitions, key, raw, view_key = "chart", command = "set") {
    let matches = definitions |: ~.name == key;
    let definition = matches[0];
    let select = selection(definition);
    let current = st.values[key];
    let value = if (command == "reset") initial([definition]).values[key]
        else if (command == "clear" and select != null) {*:current, stores: {}}
        else if (select == null) if (definition._scoped==true) {views:{*:parse.attributes(current.views),*:map([view_key,raw])}} else raw
        else if (raw.kind == select.type and raw.stores != null) raw
        else if (select.type == "point") (
            let tuples = if (raw is array) raw else [raw],
            let old = if (current.resolve=="global") [for (owner, entries in current.stores) for (entry in entries) entry]
                else if (current.stores[view_key]!=null) current.stores[view_key] else [],
            let toggled = [*(old |: not contains(tuples, ~)), for (tuple in tuples where not contains(old, tuple)) tuple],
            {*:current, stores: if (current.resolve == "global") map([view_key, if (command == "toggle") toggled else tuples])
                else {*:current.stores, *:map([view_key, if (command == "toggle") toggled else tuples])}})
        else {*:current, stores: if (current.resolve == "global") map([view_key, raw]) else {*:current.stores, *:map([view_key, raw])}};
    let invalid = util.first_error([
        if (len(matches) != 1) error("chart: command names an unknown parameter"),
        if (definition.expr != null) error("chart: computed parameters are read-only"),
        if (not contains(["set", "toggle", "clear", "reset"], command)) error("chart: unsupported parameter command"),
        if (command == "toggle" and select.type != "point") error("chart: toggle requires a point parameter"),
        if (select.type == "point") for (owner, tuples in value.stores)
            if (not (tuples is array) or any([for (tuple in tuples) not (tuple is map or tuple is element)]))
                error("chart: point update requires projected records"),
        if (select.type == "interval") for (owner, extent in value.stores)
            if (not (extent is map) or any([for (field, bounds in extent)
                if (bounds.kind == "discrete") not (bounds.values is array)
                else if (bounds.kind == "wrapped") not util.finite_number(bounds.start) or not util.finite_number(bounds.end)
                else not (bounds is array) or len(bounds) != 2 or any(bounds |> not util.finite_number(~))]))
                error("chart: interval update requires finite extents")]);
    if (invalid is error) invalid else {*:st, values: {*:st.values, *:map([key, value])},
        gesture: if (contains(["reset", "clear"], command)) null else st.gesture}
}

// chart/parse.ls — Parse and validate <chart> element tree
// Extracts structured spec from the chart element tree into a normalized map.
import expr: .expression
import util: .util

// ============================================================
// Parse a <chart> element into a normalized spec
// ============================================================

pub fn attributes(value) => if (value is element) map(value) else if (value is map) value else {}

pub fn has_attribute(value, key) =>
    len([for (name, field in attributes(value) where string(name) == key) true]) > 0

pub fn option_enabled(channel, key) {
    let enabled = channel[key ++ "_enabled"];
    if (enabled != null) enabled
    else not has_attribute(channel, key) or (channel[key] != null and channel[key] != false)
}

pub fn parse_chart(chart_el) {
    let width = if (chart_el.width != null) chart_el.width else 400;
    let height = if (chart_el.height != null) chart_el.height else 300;

    // parse padding
    let raw_padding = if (chart_el.padding != null) chart_el.padding else 20;
    let padding = if (raw_padding is int or raw_padding is float)
        {top: raw_padding, right: raw_padding, bottom: raw_padding, left: raw_padding}
    else raw_padding;

    // title
    let title = if (chart_el.title) chart_el.title else null;

    // find child elements by tag name
    let children_count = len(content(chart_el));
    let data_el = find_child(chart_el, 'data', children_count);
    let mark_el = find_child(chart_el, 'mark', children_count);
    let encoding_el = find_child(chart_el, 'encoding', children_count);
    let transform_el = find_child(chart_el, 'transform', children_count);
    let config_el = find_child(chart_el, 'config', children_count);
    let layer_el = find_child(chart_el, 'layer', children_count);
    let facet_el = find_child(chart_el, 'facet', children_count);
    let annotation_el = find_child(chart_el, 'annotation', children_count);

    // parse data: support both {values: [...]} and inline <row> children
    let data = if (data_el and data_el.values != null) data_el.values
        else if (data_el and len(content(data_el)) > 0)
            [for (i in 0 to (len(content(data_el)) - 1),
                  let child = data_el[i]
                  where child != null) child]
        else null;

    // parse mark
    let mark = if (mark_el) parse_mark(mark_el) else null;

    // parse encoding channels
    let encoding = if (encoding_el) parse_encoding(encoding_el) else {};

    // parse layer (composition)
    let layer = if (layer_el) parse_layer(layer_el) else null;

    {
        width: width,
        height: height,
        _width_specified: chart_el.width != null,
        _height_specified: chart_el.height != null,
        aspect_ratio: chart_el.aspect_ratio,
        padding: padding,
        title: title,
        id: chart_el.id,
        coordinate: if (chart_el.coordinate != null) chart_el.coordinate else attributes(find_child(chart_el, 'coordinate', children_count)),
        interaction: chart_el.interaction,
        state: chart_el.state,
        animate: chart_el.animate,
        timeline: chart_el.timeline,
        data: data,
        data_source: attributes(data_el),
        datasets: chart_el.datasets,
        params: if (chart_el.params != null) chart_el.params else (
            let params = find_child(chart_el, 'params', children_count),
            if (params != null) [for (param in content(params)) attributes(param)] else null),
        projection: chart_el.projection,
        resolve: chart_el.resolve,
        clip: chart_el.clip,
        mark: mark,
        encoding: encoding,
        transform: transform_el,
        config: config_el,
        layer: layer,
        facet: facet_el,
        annotation: annotation_el
    }
}

// ============================================================
// Find a child element by tag name
// ============================================================

fn find_child(parent_el, tag_name, count) {
    let matches = [for (i in 0 to (count - 1),
                        let child = parent_el[i]
                        where child and name(child) == tag_name) child]
    if (len(matches) > 0) matches[0] else null
}

// ============================================================
// Parse mark element
// ============================================================

pub fn parse_mark(mark_el) {
    // Preserve mark-specific options for specialized renderers such as wordcloud.
    let attrs = attributes(mark_el);
    let mk = if (mark_el.kind) mark_el.kind else mark_el['type'];
    {
        *:attrs,
        kind: mk,
        inner_radius: if (mark_el.inner_radius != null) mark_el.inner_radius else 0
    }
}

// ============================================================
// Parse encoding element
// ============================================================

fn parse_encoding(encoding_el) {
    let count = len(content(encoding_el));
    map([for (key in ["x", "y", "color", "size", "opacity", "theta", "text", "stroke", "x_offset",
        "x2", "y2", "detail", "tooltip", "shape", "order", "url", "theta2", "radius", "radius2", "longitude", "latitude",
        "position", "key", "group_key", "path", "direction", "magnitude", "enter_delay", "enter_duration", "update_delay", "update_duration", "exit_delay", "exit_duration"],
        let child = find_child(encoding_el, symbol(key), count))
        for (part in [key, if (child != null) parse_channel(child) else null]) part])
}

pub fn parse_channel(ch_el) {
    {
        *:attributes(ch_el),
        field: ch_el.field,
        dtype: ch_el.dtype,
        value: ch_el.value,
        datum: ch_el.datum,
        title: ch_el.title,
        format: ch_el.format,
        sort: ch_el.sort,
        aggregate: ch_el.aggregate,
        stack: ch_el.stack,
        bin: ch_el.bin,
        zero: ch_el.zero,
        scale: ch_el.scale,
        axis: ch_el.axis,
        legend: ch_el.legend,
        condition: ch_el.condition,
        axis_enabled: option_enabled(ch_el, "axis"),
        legend_enabled: option_enabled(ch_el, "legend"),
        scale_enabled: option_enabled(ch_el, "scale")
    }
}

// ============================================================
// Parse layer (composition)
// ============================================================

fn parse_layer(layer_el) {
    let count = len(content(layer_el));
    [for (i in 0 to (count - 1),
          let child = layer_el[i]
          where name(child) == 'chart') parse_chart(child)]
}

// ============================================================
// Get encoding channel by name
// ============================================================

pub fn get_channel(encoding, channel_name: string) {
    encoding[channel_name]
}

pub fn channel_value(channel, row, fallback = null) {
    let resolved = channel_definition(channel, row);
    if (resolved is error) resolved
    else if (resolved and resolved.value != null) resolved.value
    else if (resolved and resolved.datum != null) resolved.datum
    else if (resolved and resolved.field) row[resolved.field]
    else fallback
}

// Conditions share the filter predicate vocabulary and may use a pure Lambda callback (S11.4.11).
pub fn test_predicate(predicate, row) {
    if (predicate is fn) predicate(row)
    else if (predicate is bool) predicate
    else if (predicate is error) predicate
    else if (predicate is string) test_predicate(prepare_predicate(predicate), row)
    else if (predicate._expression != null) expr.test(predicate._expression, row)
    else if (predicate.test != null) test_predicate(predicate.test, row)
    else if (predicate['and'] != null)
        test_parts(predicate['and'], row, 0, true)
    else if (predicate['or'] != null)
        test_parts(predicate['or'], row, 0, false)
    else if (predicate['not'] != null) {
        let result = test_predicate(predicate['not'], row);
        if (result is error) result else not result
    }
    else if (predicate.field != null) {
        let actual = row[predicate.field];
        let op = predicate.op;
        let expected = predicate.value;
        if (has_attribute(predicate, "equal")) actual == predicate.equal
        else if (has_attribute(predicate, "oneOf")) len([for (v in predicate.oneOf where v == actual) v]) > 0
        else if (has_attribute(predicate, "range")) actual >= predicate.range[0] and actual <= predicate.range[1]
        else if (has_attribute(predicate, "valid")) (actual != null and not (actual is error)) == predicate.valid
        else if (predicate.gt != null) actual > predicate.gt
        else if (predicate.gte != null) actual >= predicate.gte
        else if (predicate.lt != null) actual < predicate.lt
        else if (predicate.lte != null) actual <= predicate.lte
        else if (op == "==") actual == expected
        else if (op == "!=") actual != expected
        else if (op == ">") actual > expected
        else if (op == ">=") actual >= expected
        else if (op == "<") actual < expected
        else if (op == "<=") actual <= expected
        else false
    } else false
}

pub fn channel_definition(channel, row) {
    let conditions = if (channel.condition is array) channel.condition else [channel.condition];
    first_condition(conditions, row, 0, channel)
}

fn first_condition(conditions, row, index, fallback) {
    if (index >= len(conditions)) fallback else {
        let condition = conditions[index];
        let passed = if (condition == null) false else test_predicate(condition, row);
        if (passed is error) passed else if (passed) condition else first_condition(conditions, row, index + 1, fallback)
    }
}

fn test_parts(parts, row, index, conjunction) {
    if (index >= len(parts)) conjunction else {
        let passed = test_predicate(parts[index], row);
        if (passed is error) passed else if (passed != conjunction) passed
        else test_parts(parts, row, index + 1, conjunction)
    }
}

// Compile all branches even for empty data; malformed input is a value error (S7.4.1).
pub fn prepare_predicate(predicate) {
    if (predicate is string) {
        let compiled = expr.compile(predicate);
        if (compiled is error) compiled else {_expression: compiled}
    } else if (predicate.test != null) {
        let test = prepare_predicate(predicate.test);
        if (test is error) test else {*:attributes(predicate), test: test}
    } else if (predicate['not'] != null) {
        let test = prepare_predicate(predicate['not']);
        if (test is error) test else {*:attributes(predicate), 'not': test}
    } else if (predicate['and'] != null or predicate['or'] != null) {
        let key = if (predicate['and'] != null) "and" else "or";
        let parts = [for (part in predicate[key]) prepare_predicate(part)];
        let failure = util.first_error(parts);
        if (failure is error) failure else {*:attributes(predicate), *:map([key, parts])}
    } else predicate
}

fn prepare_channel(channel) {
    if (channel is array) {
        let prepared = [for (item in channel) prepare_channel(item)];
        let failure = util.first_error(prepared);
        if (failure is error) failure else prepared
    } else if (channel.condition == null) channel else {
        let conditions = if (channel.condition is array) channel.condition else [channel.condition];
        let prepared = [for (condition in conditions) prepare_predicate(condition)];
        let failure = util.first_error(prepared);
        if (failure is error) failure else {*:attributes(channel), condition: prepared}
    }
}

pub fn prepare_channels(encoding) {
    let entries = [for (key, channel in encoding) {key: string(key), value: prepare_channel(channel)}];
    let failure = util.first_error(entries |> ~.value);
    if (failure is error) failure else map([for (entry in entries) for (item in [entry.key, entry.value]) item])
}

pub fn validate_conditions(encoding, data) {
    util.first_error([for (key, channel in encoding)
        for (entry in (if (channel is array) channel else [channel]) where entry.condition != null)
            for (row in data) channel_definition(entry, row)])
}

// check if encoding has a specific channel
pub fn has_channel(encoding, channel_name: string) bool {
    encoding[channel_name] != null
}

// ============================================================
// Parse concat composition (<hconcat> or <vconcat>)
// ============================================================

pub fn parse_concat(concat_el) {
    let direction = if (name(concat_el) == 'hconcat') "horizontal" else "vertical";
    let spacing = if (concat_el.spacing != null) concat_el.spacing else 20;
    let count = len(content(concat_el));
    let children = [for (i in 0 to (count - 1),
                         let child = concat_el[i]
                         where child != null and name(child) in ['chart', 'hconcat', 'vconcat', 'repeat', 'svg'])
                         if (name(child) == 'svg') child else parse_top(child)];
    {
        *:parse_chart(concat_el),
        concat: direction,
        spacing: spacing,
        children: children
    }
}

// ============================================================
// Parse repeat composition (<repeat>)
// ============================================================

pub fn parse_repeat(repeat_el) {
    let count = len(content(repeat_el));
    let children = [for (i in 0 to (count - 1),
                        let child = repeat_el[i]
                        where child != null) child];
    let row_el = [for (c in children where name(c) == 'row') c];
    let col_el = [for (c in children where name(c) == 'column') c];
    let row_fields = if (len(row_el) > 0) row_el[0][0] else null;
    let col_fields = if (len(col_el) > 0) col_el[0][0] else null;
    let template = [for (c in children where name(c) == 'chart') c];
    let tmpl = if (len(template) > 0) parse_chart(template[0]) else null;
    {
        *:parse_chart(repeat_el),
        repeat_row: row_fields,
        repeat_column: col_fields,
        template: tmpl
    }
}

// ============================================================
// Top-level parse: detects chart, hconcat, vconcat, or repeat
// ============================================================

pub fn parse_top(el) {
    let tag = name(el);
    if (tag == 'hconcat' or tag == 'vconcat') parse_concat(el)
    else if (tag == 'repeat') parse_repeat(el)
    else parse_chart(el)
}

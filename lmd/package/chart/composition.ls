// Resolve dataflow and scale/guide ownership before any view is measured or drawn.
import parse: .parse
import cfg: .config
import source: .source
import transform: .transform
import scale: .scale
import sizing: .sizing
import util: .util
import geometry: .geometry

let channels = ["x", "y", "color", "stroke", "size", "shape", "opacity", "theta", "radius"]

fn kind(spec) => if (spec.concat != null) "concat" else if (spec.repeat_row != null or spec.repeat_column != null) "repeat"
    else if (spec.facet != null) "facet" else if (spec.layer != null) "layer" else "unit"

fn inherit_view(spec, parent) {
    let datasets = {*:parse.attributes(parent.datasets), *:parse.attributes(spec.datasets)};
    let own_data = spec.data != null or spec.data_source.values != null or spec.data_source.name != null or spec.data_source.url != null;
    let resolved = if (parent == null or own_data) source.resolve(spec.data, spec.data_source, datasets) else parent.data;
    let raw = if (resolved is error) resolved else if (spec.mark.kind == "geoshape" or spec.mark.kind == "geo" or
        resolved.type == "FeatureCollection" or resolved.type == "Feature") geometry.records(resolved) else resolved;
    let data = if (raw is error) raw else if (not (raw is array)) error("chart: data must be an array")
        else transform.apply_transforms(raw, spec.transform, datasets);
    let padding = sizing.normalize_padding(spec.padding);
    {*:spec, data: if (data is error) data else if (padding is error) padding else data, transform: null,
        padding: padding, datasets: datasets, config: cfg.inherit(parent.config, spec.config),
        encoding: {*:cfg.settings(parent.encoding), *:cfg.settings(spec.encoding)}}
}

fn child_bindings(spec, inherited, path, form) {
    let scales = map([for (key in channels,
        let policy = spec.resolve.scale[key],
        let shared = policy == "shared" or (policy == null and (inherited.scale[key] != null or
            form == "layer" or form == "facet" or (form == "repeat" and key != "x" and key != "y"))))
        for (part in [key, if (shared) (if (inherited.scale[key] != null) inherited.scale[key] else path ++ ":" ++ key) else null]) part]);
    {scale: scales, *:map([for (guide in ["axis", "legend"])
        for (part in [guide, map([for (key in channels,
            let policy = spec.resolve[guide][key],
            let shared = policy == "shared" or (policy == null and (inherited[guide][key] != null or form == "layer")))
            for (item in [key, if (shared) (if (inherited[guide][key] != null) inherited[guide][key] else path ++ ":" ++ guide ++ ":" ++ key) else null]) item])]) part])}
}

fn leaf_groups(bindings, path) {
    let scales = map([for (key in channels) for (part in [key, if (bindings.scale[key] != null) bindings.scale[key] else path ++ ":" ++ key]) part]);
    {scale: scales, *:map([for (guide in ["axis", "legend"]) for (part in [guide,
        map([for (key in channels) for (item in [key,
            (if (bindings[guide][key] != null) bindings[guide][key] else path ++ ":" ++ guide ++ ":" ++ key) ++ ":" ++ scales[key]]) item])]) part])}
}

fn facet_plan(spec) {
    let facet = spec.facet;
    let row_field = if (facet.row is string) facet.row else facet.row.field;
    let column_field = if (facet.column is string) facet.column else facet.column.field;
    let grid = row_field != null or column_field != null;
    let rows = if (row_field != null) util.unique_vals(spec.data |> ~[row_field]) else [null];
    let columns = if (column_field != null) util.unique_vals(spec.data |> ~[column_field]) else [null];
    let keys = if (grid) [for (row in rows) for (column in columns) [row, column]] else util.unique_vals(spec.data |> ~[facet.field]);
    {columns: if (grid) max([1, len(columns)]) else if (facet.columns != null) facet.columns else 3,
        headers: [for (key in keys) if (grid) join([for (value in key where value != null) string(value)], " / ") else string(key)],
        data: [for (key in keys) if (grid) (spec.data |: (row_field == null or ~[row_field] == key[0]) and
            (column_field == null or ~[column_field] == key[1])) else (spec.data |: ~[facet.field] == key)]}
}

// Repeat substitution reaches every field-bearing option, including nested views and conditions.
fn substitute(value, row_field, column_field) {
    if (value is array) [for (entry in value) substitute(entry, row_field, column_field)]
    else if (value is map) map([for (key, entry in value) for (part in [string(key),
        if (string(key) == "field" and entry.repeat != null) (if (entry.repeat == "column") column_field else row_field)
        else substitute(entry, row_field, column_field)]) part])
    else value
}

pub fn prepare(raw_spec, prepare_mark, path = "chart", parent = null, bindings = null) {
    if (raw_spec is error) raw_spec
    else if (not (raw_spec is map or raw_spec is element)) error("chart: specification must be a map or element")
    else if (raw_spec is element and name(raw_spec) == 'svg') raw_spec
    else {
        let parsed = if (raw_spec is element) parse.parse_top(raw_spec) else raw_spec;
        let spec = {*:inherit_view(parsed, parent), _view_path: path};
        let form = kind(spec);
        let children = child_bindings(spec, bindings, path, form);
        let resolution_error = util.first_error([for (section in ["scale", "axis", "legend"]) for (key, policy in spec.resolve[section]
            where not contains(if (section == "axis") ["x", "y"] else if (section == "legend") ["color", "size", "shape"] else channels, string(key)) or
                (policy != "shared" and policy != "independent"))
            error("chart: resolution requires a supported channel and shared or independent")]);
        if (spec.data is error) {*:spec, _preparation_error: spec.data}
        else if (resolution_error is error) {*:spec, _preparation_error: resolution_error}
        else if (form == "concat") {*:spec, children: [for (index, child in spec.children)
            if (child is element and name(child) == 'svg') child
            else prepare(sizing.inherit_requests(spec, child), prepare_mark, path ++ "-c" ++ string(index), spec, children)]}
        else if (form == "repeat") {
            let rows = if (spec.repeat_row != null) spec.repeat_row else [""];
            let columns = if (spec.repeat_column != null) spec.repeat_column else [""];
            let template = if (spec.template is element) parse.parse_top(spec.template) else spec.template;
            if (not (rows is array) or not (columns is array) or len(rows) == 0 or len(columns) == 0 or not (template is map))
                {*:spec, _preparation_error: error("chart: repeat requires nonempty field arrays and a chart template")}
            else {*:spec, _views: [for (ri, row_field in rows) for (ci, column_field in columns)
                prepare(sizing.inherit_requests(spec, substitute(template, row_field, column_field)), prepare_mark,
                    path ++ "-r" ++ string(ri * len(columns) + ci), spec, children)]}
        } else if (form == "facet") {
            let plan = facet_plan(spec);
            if (not util.finite_number(plan.columns) or plan.columns < 1 or floor(plan.columns) != plan.columns or
                (spec.facet.spacing != null and (not util.finite_number(spec.facet.spacing) or spec.facet.spacing < 0)))
                {*:spec, _preparation_error: error("chart: facet requires positive integer columns and nonnegative spacing")}
            else {*:spec, _facet_plan: plan, _views: [for (index, rows in plan.data)
                prepare({*:spec, data: rows, facet: null, title: null}, prepare_mark, path ++ "-f" ++ string(index), null, children)]}
        } else if (form == "layer") {
            let views = [for (index, child in spec.layer) prepare(child, prepare_mark, path ++ "-l" ++ string(index), spec, children)];
            let invalid = [for (child in views where kind(child) != "unit" and kind(child) != "layer") child];
            {*:spec, _views: views, _preparation_error: if (len(invalid) > 0) error("chart: layer children must share one plot") else null}
        } else {
            let defaults = if (spec.mark.kind != "trail" or spec.encoding.size.field == null or
                not parse.option_enabled(spec.encoding.size, "scale") or spec.encoding.size.scale.range != null) spec.encoding
                else {*:spec.encoding, size: {*:spec.encoding.size, scale: {*:parse.attributes(spec.encoding.size.scale), range: [1, 10]}}};
            let encoding = if (spec.mark.kind == "trail" and defaults.size != null)
                {*:defaults, size: {*:defaults.size, _size_unit: "width"}} else defaults;
            let encoded = transform.prepare_encoding(spec.data, encoding);
            let prepared = if (encoded.data is error) {*:spec, data: encoded.data} else prepare_mark({*:spec, *:encoded});
            {*:prepared, _groups: leaf_groups(bindings, path), _mark_prepared: true,
                _preparation_error: if (prepared.data is error) prepared.data else null}
        }
    }
}

pub fn leaves(spec) {
    if (spec is element) []
    else if (spec.concat != null) [for (child in spec.children) for (leaf in leaves(child)) leaf]
    else if (spec._views != null) [for (child in spec._views) for (leaf in leaves(child)) leaf]
    else [spec]
}

fn preparation_error(spec) => if (spec is error) spec else if (spec is element) null else util.first_error([spec._preparation_error,
    for (child in spec.children) preparation_error(child), for (child in spec._views) preparation_error(child)])

pub fn visual_mapping(views, key) {
    let candidates = [for (leaf in views where (leaf.encoding[key].field != null or leaf.encoding[key].datum != null) and
        (key != "size" or leaf.mark.kind != "wordcloud")) leaf];
    let first_view = candidates[0];
    let channel = first_view.encoding[key];
    let rows = [for (leaf in candidates) for (row in leaf.data) {value: parse.channel_value(leaf.encoding[key], row)}];
    let endpoints = [for (leaf in (if (key == "theta" or key == "radius") candidates else []),
        let secondary = leaf.encoding[key ++ "2"] where secondary != null and secondary.value == null)
        for (row in leaf.data) {value: parse.channel_value(secondary, row)}];
    let normalized = {*:parse.attributes(channel), field: "value"};
    if (len(candidates) == 0) null
    else if (key == "color" or key == "stroke") scale.infer_color_scale(normalized, rows)
    else if (key == "shape") scale.shape_scale(normalized, rows)
    else if (key == "theta") scale.angular_scale(normalized, [*rows, *endpoints])
    else if (key == "radius") scale.radius_scale(normalized, [*rows, *endpoints])
    else scale.visual_scale(normalized, rows, if (key == "opacity") 0.2 else 20.0, if (key == "opacity") 1.0 else 200.0)
}

fn mapping_groups(views) {
    [for (key in channels) for (group in util.unique_vals([for (leaf in views where leaf.encoding[key] != null) leaf._groups.scale[key]]),
        let members = [for (leaf in views where leaf._groups.scale[key] == group and
            (leaf.encoding[key].field != null or leaf.encoding[key].datum != null) and
            parse.option_enabled(leaf.encoding[key], "scale")) leaf],
        let types = util.unique_vals([for (leaf in members where leaf.encoding[key].dtype != null) leaf.encoding[key].dtype]),
        let units = if (key == "size") util.unique_vals([for (leaf in members)
            if (leaf.encoding.size._size_unit != null) leaf.encoding.size._size_unit else "area"]) else [],
        let mapping = if (len(types) > 1 or len(units) > 1) error("chart: shared scales require compatible channel types and units")
            else if (len(members) == 0) null else if (key == "x" or key == "y") scale.shared_position(members, key, 0.0, 1.0) else visual_mapping(members, key))
        {key: key, group: group, mapping: mapping, options: members[0].encoding[key].scale}]
}

fn first_guide(leaf, key, guide, views) {
    let eligible = [for (candidate in views where candidate._groups[guide][key] == leaf._groups[guide][key] and
        (candidate.encoding[key].field != null or candidate.encoding[key].datum != null) and
        candidate.encoding[key].value == null and parse.option_enabled(candidate.encoding[key], guide) and
        (guide != "legend" or candidate.mark.kind != "wordcloud") and
        parse.option_enabled(candidate.encoding[key], "scale")) candidate._view_path];
    len(eligible) == 0 or eligible[0] == leaf._view_path
}

fn resolve_tree(spec, groups, views) {
    if (spec is element) spec
    else if (spec.concat != null) {*:spec, children: [for (child in spec.children) resolve_tree(child, groups, views)]}
    else if (spec._views != null) {*:spec, _views: [for (child in spec._views) resolve_tree(child, groups, views)]}
    else {*:spec, encoding: map([for (key, channel in spec.encoding,
        let channel_name = string(key),
        let choices = [for (group in groups where group.key == channel_name and group.group == spec._groups.scale[channel_name]) group],
        let group = choices[0],
        let mapping = group.mapping,
        let guide = if (channel_name == "x" or channel_name == "y") "axis" else "legend",
        let shared = if (mapping == null or mapping.kind == "identity" or not parse.option_enabled(channel, "scale")) channel
            else {*:channel, scale: {*:parse.attributes(group.options), type: mapping.kind, domain: mapping.domain,
                *:if (guide != "legend" or channel_name == "theta" or channel_name == "radius") {} else if (mapping.scheme != null) {range: mapping.scheme, reverse: mapping.reverse}
                    else {range: mapping.range, reverse: false}}})
        for (part in [channel_name, if (not contains(channels, channel_name)) channel
            else {*:shared, *:map([guide ++ "_enabled", parse.option_enabled(channel, guide) and first_guide(spec, channel_name, guide, views)])}]) part])}
}

pub fn resolve(spec) {
    let failure = preparation_error(spec);
    if (failure is error) failure else {
        let views = leaves(spec);
        let groups = mapping_groups(views);
        let mapping_error = util.first_error(groups |> ~.mapping);
        if (mapping_error is error) mapping_error else resolve_tree(spec, groups, views)
    }
}

// Resolve dataflow and scale/guide ownership before any view is measured or drawn.
import parse: .parse
import cfg: .config
import transform: .transform
import scale: .scale
import sizing: .sizing
import util: .util
import geometry: .geometry
import parameter: .parameter
import composite: .composite
import coordinate: .coordinate
import behavior: .behavior
import animation: .animation
import dataflow:.dataflow

let channels = ["x", "y", "color", "stroke", "size", "shape", "opacity", "theta", "radius"]

fn kind(spec) => if (spec.concat != null) "concat" else if (spec.repeat_row != null or spec.repeat_column != null) "repeat"
    else if (spec.facet != null) "facet" else if (spec.layer != null) "layer" else "unit"

fn inherit_view(spec, parent) {
    let definitions = behavior.definitions(spec, if (spec._parameters != null) spec._parameters
        else if (parent._parameters != null) parent._parameters else spec.params);
    let st = if (spec._parameter_state != null) spec._parameter_state else parent._parameter_state;
    let values = if (spec._parameter_values != null) spec._parameter_values else parent._parameter_values;
    let interactive = spec._interactive == true or parent._interactive == true;
    let resolved=dataflow.resolve(spec,parent,values);
    let padding = sizing.normalize_padding(spec.padding);
    {*:spec, *:resolved, data: if (resolved.data is error) resolved.data else if (padding is error) padding else resolved.data, transform: null,
        padding: padding, config: cfg.inherit(parent.config, spec.config),
        coordinate: if (coordinate.enabled(spec.coordinate)) spec.coordinate else if (coordinate.enabled(spec.mark.coordinate)) spec.mark.coordinate else parent.coordinate,
        _partition:if (spec._partition!=null) spec._partition else parent._partition,
        _qualified_id:if (spec.id!=null and (spec._partition!=null or parent._partition!=null)) spec.id++"["++
            format((if (spec._partition!=null) spec._partition else parent._partition).key,{type:"json",compact:true})++"]"
            else if (spec._qualified_id!=null) spec._qualified_id else parent._qualified_id,
        projection:behavior.camera_options({*:spec,_parameter_state:st},parent.projection),
        _all_behaviors: if (spec._all_behaviors != null) spec._all_behaviors else parent._all_behaviors,
        state: behavior.merge(parent.state, spec.state),
        animate: animation.merge(animation.merge(parent.animate, spec.animate), spec.mark.animate),
        _time: if (spec._time != null) spec._time else parent._time,
        _requires_key:spec._requires_key==true or parent._requires_key==true,
        _timing_values: if (spec._timing_values != null) spec._timing_values else parent._timing_values,
        _parameters: definitions, _parameter_state: st, _parameter_values: values, _interactive: interactive,
        encoding: (let encoding = {*:cfg.settings(parent.encoding), *:cfg.settings(spec.encoding)},
            behavior.bind_guides(parameter.encoding(encoding, values, definitions, st, spec._view_path, interactive),spec))}
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


pub fn prepare(raw_spec, prepare_mark, path = "chart", parent = null, bindings = null) {
    if (raw_spec is error) raw_spec
    else if (not (raw_spec is map or raw_spec is element)) error("chart: specification must be a map or element")
    else if (raw_spec is element and name(raw_spec) == 'svg') raw_spec
    else {
        let parsed = if (raw_spec is element) parse.parse_top(raw_spec) else raw_spec;
        let inherited = {*:inherit_view({*:parsed, _view_path: path}, parent), _view_path: path};
        let spec = if (inherited.facet!=null) inherited else composite.expand(inherited);
        let form = kind(spec);
        let children = child_bindings(spec, bindings, path, form);
        let resolution_error = util.first_error([for (section in ["scale", "axis", "legend"]) for (key, policy in spec.resolve[section]
            where not contains(if (section == "axis") ["x", "y"] else if (section == "legend") ["color", "size", "shape"] else channels, string(key)) or
                (policy != "shared" and policy != "independent"))
            error("chart: resolution requires a supported channel and shared or independent")]);
        if (spec is error) {_preparation_error: spec}
        else if (spec.data is error) {*:spec, _preparation_error: spec.data}
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
                prepare({*:sizing.inherit_requests(spec, if (spec._factory_catalogue!=null) spec._factory_catalogue[ri*len(columns)+ci]
                        else parse.substitute(template, row_field, column_field)),_partition:{key:[row_field,column_field]}},prepare_mark,
                    path++"-r"++util.binding_key([row_field,column_field]),spec,children)]}
        } else if (form == "facet") {
            let plan = dataflow.facet_plan(spec);
            if (not util.finite_number(plan.columns) or plan.columns < 1 or floor(plan.columns) != plan.columns or
                (spec.facet.spacing != null and (not util.finite_number(spec.facet.spacing) or spec.facet.spacing < 0)))
                {*:spec, _preparation_error: error("chart: facet requires positive integer columns and nonnegative spacing")}
            else {*:spec, _facet_plan: plan, _views: [for (index, rows in plan.data)
                prepare({*:if (spec._factory_catalogue!=null) spec._factory_catalogue[index] else {*:spec,data:rows}, facet:null,title:null,
                    _factory_catalogue:null,_partition:{key:plan.keys[index]},_qualified_id:(if (spec.id!=null) spec.id else path)++"["++format(plan.keys[index],{type:"json",compact:true})++"]"},
                    prepare_mark,path++"-f"++util.binding_key(plan.keys[index]),spec,children)]}
        } else if (form == "layer") {
            let coordinates = [for (child in spec.layer, let parsed = if (child is element) parse.parse_top(child) else child,
                let options = if (coordinate.enabled(parsed.coordinate)) parsed.coordinate else parsed.mark.coordinate
                where coordinate.enabled(options)) options];
            let options = if (coordinate.enabled(spec.coordinate)) spec.coordinate else coordinates[0];
            let models = [for (options in [options, *coordinates] where coordinate.enabled(options)) coordinate.configure(options, 1, 1)];
            let failure = util.first_error(models);
            let views = [for (index, child in spec.layer) prepare(child, prepare_mark, path ++ "-l" ++ string(index), {*:spec, coordinate: options}, children)];
            let invalid = [for (child in views where kind(child) != "unit" and kind(child) != "layer") child];
            {*:spec, coordinate: options, _views: views, _preparation_error: if (failure is error) failure
                else if (len(util.unique_vals(models)) > 1) error("chart: layered marks require one compatible coordinate")
                else if (len(invalid) > 0) error("chart: layer children must share one plot") else null}
        } else {
            let width_mark=contains(["trail","link","vector","path"],spec.mark.kind);
            let defaults = if (not width_mark or spec.encoding.size.field == null or
                not parse.option_enabled(spec.encoding.size, "scale") or spec.encoding.size.scale.range != null) spec.encoding
                else {*:spec.encoding, size: {*:spec.encoding.size, scale: {*:parse.attributes(spec.encoding.size.scale), range: [1, 10]}}};
            let encoding = if (width_mark and defaults.size != null)
                {*:defaults, size: {*:defaults.size, _size_unit: "width"}} else defaults;
            let encoded = transform.prepare_encoding(spec.data, encoding);
            let filtered = if (encoded.data is error) encoded.data else behavior.filter_data(spec, encoded.data);
            let prepared = if (filtered is error) {*:spec, data: filtered} else prepare_mark({*:spec, *:encoded, data: filtered});
            {*:prepared, _domain_data: if (behavior.retain_domains(spec)) encoded.data else prepared._domain_data, _groups: leaf_groups(bindings, path), _mark_prepared: true,
                _preparation_error: if (prepared.data is error) prepared.data else animation.validate(prepared)}
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
    // conditional literal styling must not replace field values in the scale/legend domain.
    let rows = [for (leaf in candidates) for (value in scale.channel_values(leaf.encoding[key], if (leaf._domain_data != null) leaf._domain_data else leaf.data)) {value: value}];
    let endpoints = [for (leaf in (if (key == "theta" or key == "radius") candidates else []),
        let secondary = leaf.encoding[key ++ "2"] where secondary != null and secondary.value == null)
        for (value in scale.channel_values(secondary, if (leaf._domain_data != null) leaf._domain_data else leaf.data)) {value: value}];
    let normalized = {*:parse.attributes(channel), field: "value"};
    if (len(candidates) == 0) null
    else scale.visual_mapping(key, normalized, [*rows, *endpoints])
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
            else if (len(members) == 0) null else if (key == "x" or key == "y") scale.shared_position([for (leaf in members) if (leaf._domain_data != null) {*:leaf, data: leaf._domain_data} else leaf], key, 0.0, 1.0) else visual_mapping(members, key))
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

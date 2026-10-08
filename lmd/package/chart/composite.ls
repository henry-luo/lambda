// Factories expand once into values; a returned composite cannot recursively expand itself.
import parse: .parse
import util: .util

fn recursive(spec) {
    let value = if (spec is element) parse.parse_top(spec) else spec;
    value.mark.kind == "composite" or any([for (child in [*value.layer, *value.children, value.template]
        where child != null) recursive(child)])
}

pub fn expand(spec) {
    if (spec.mark.kind != "composite") spec
    else if (not (spec.mark.expand is fn)) error("chart: composite mark requires a pure expand function")
    else {
        let expanded = spec.mark.expand(spec.data, if (spec.mark.options != null) spec.mark.options else {});
        let parsed = if (expanded is element) parse.parse_top(expanded) else expanded;
        if (expanded is error) expanded
        else if (not (parsed is map) or (parsed.mark == null and parsed.layer == null))
            error("chart: composite factory must return a chart or layer composition")
        else if (recursive(parsed)) error("chart: recursive composite expansion is not supported")
        else {*:spec, mark: null, *:parsed, data: if (parsed.data != null) parsed.data else spec.data,
            _composite: true}
    }
}

import elements: lambda.ui.dtna.elements

// UI descriptors retain source identity; presentation never owns application data (S12.1.3).
pub let common_props = ["id", "class", "style", "label", "title", "role", "tabindex"]

pub fn fail(kind, detail, family = "dtna") error => error(family ++ ": " ++ string(kind) ++ ": " ++ detail)
// key membership preserves an explicitly authored null (S8.2.2v5).
pub fn has(props, key) => key at props
pub fn option(props, key, fallback) => if (has(props,key)) props[key] else fallback
pub fn text(value) string => if (value == null) "" else string(value)
pub fn px(value) => string(value) ++ "px"
// dimensions accept a single nonnegative CSS length; arbitrary declarations are never lengths.
type css_unsigned_number = \("+"? ("\d"+ ("." "\d"*)? | "." "\d"+) (("e" | "E") ("+" | "-")? "\d"+)?)
let css_length_units = ["px","em","rem","%","vw","vh","vmin","vmax","ch","ex","cm","mm","in","pt","pc"]
pub fn css_number(value) => if (value is string and value is css_unsigned_number)
    // out-of-range conversion is a diagnostic, so predicates must turn it into false explicitly.
    finite(float(value) ^ {null}) else false
pub fn css_length(value) => if (value is number) finite(value) and value >= 0 else if (not (value is string)) false
    else (let source = lower(trim(value)),
        let units = [for (unit in css_length_units where ends_with(source,unit)) unit],
        source == "0" or any([for (unit in units) css_number(slice(source,0,len(source)-len(unit)))]))
pub fn length(value) => if (value is number) px(value) else trim(value)
pub fn boolean_attr(key, enabled) => if (enabled == true) {[key]:""} else {}
pub fn aria(value) => if (value == true) "true" else "false"
pub fn children(value) => if (value is array) value else if (value == null) [] else [value]
pub fn render(value) => if (value is array) [for (child in value) render(child)] else if (value is element) apply(value)
    else if (value is number or value is bool or value is symbol) text(value) else value
pub fn contents(node) => [for (child in content(node)) render(child)]

// element keys are symbols; normalize to constructor map keys for presence checks (S8.2.2v5).
// name() reads tag metadata even when an input has an authored name attribute.
// explicit kinds remain supported for the other style families' descriptors.
pub fn kind(node) {
    let tag = string(name(node))
    let separator = index_of(tag,".");
    if (separator >= 0) symbol(replace(slice(tag,separator+1),"_","-")) else node.kind
}
pub fn props(node) => if (node.kind != null and node.props is map) node.props else
    map([for (key,value in map(node) where node.kind == null or string(key) != "kind") (string(key),value)])
pub fn heading(attributes, children, level) {
    // explicit HTML tags share their attribute/content contract across families.
    if (level == 1) <h1 *:attributes,*children>
    else if (level == 2) <h2 *:attributes,*children>
    else if (level == 3) <h3 *:attributes,*children>
    else if (level == 4) <h4 *:attributes,*children>
    else if (level == 5) <h5 *:attributes,*children>
    else <h6 *:attributes,*children>
}

pub fn properties(props, allowed: array, kind, attributes = false, family = "dtna") bool^ {
    if (not (props is map)) raise fail(kind,"props must be a map",family)
    else (
        let invalid = [for (key, value in props where not contains(allowed,string(key)) and
            not (attributes and (starts_with(string(key),"aria-") or starts_with(string(key),"data-")))) string(key)],
        if (len(invalid) > 0) raise fail(kind,"unsupported prop " ++ invalid[0],family) else true
    )
}
pub fn node(kind, props, child, allowed = []) element^ {
    let names = properties(props,[*common_props,*allowed],kind,true)^
    let values = validate(kind,props)^;
    elements.create(kind,props,children(child))
}

pub fn attrs(props) => map([for (key, value in props where contains(["id", "role", "tabindex"], string(key)) or (string(key) == "title" and value is string) or
    starts_with(string(key), "aria-") or starts_with(string(key), "data-")) (key, value)])
// style families share attribute policy without sharing selector namespaces.
pub fn classes(kind, props, extra = "", family = "dtna") => family ++ "-" ++ string(kind) ++
    (if (props.class == null) "" else " " ++ text(props.class)) ++
    (if (props.size is string or props.size is symbol) " " ++ family ++ "-size-" ++ text(props.size) else "") ++
    (if (props.status == null) "" else " " ++ family ++ "-status-" ++ text(props.status)) ++
    (if (props.variant == null) "" else " " ++ family ++ "-variant-" ++ text(props.variant)) ++
    (if (extra == "") "" else " " ++ extra)
pub fn styled(node, suffix = "", family = "dtna") => {*:attrs(props(node)), class:classes(kind(node), props(node), suffix, family), style:props(node).style}
pub fn style_with(props, extra) => extra ++ text(props.style)
pub fn action(node, action, value = null) => {component:string(kind(node)), id:props(node).id, action:action, value:value}

// semantic slots share validation and merging rather than silently ignoring misspelled parts.
pub fn validate_parts(props, parts, owner) bool^ {
    let bad = [for (field in ["class_names","styles"] where has(props,field) and
        (not (props[field] is map) or any([for (key,value in props[field])
            not contains(parts,string(key)) or not (value is string)]))) field];
    if (len(bad) > 0) raise fail(owner,bad[0] ++ " must map known semantic parts to strings") else true
}
pub fn part_attrs(props, part, base) => {
    class:base ++ (if (props.class_names[part] == null) "" else " " ++ props.class_names[part]),
    style:props.styles[part]
}

pub fn enum_valid(value, choices: array) bool^ => value == null or contains(choices, text(value))^
pub fn finite(value) => value is number and not (value is nan) and not (value is inf) and value != -inf
pub fn css_color(value) => value is string and len(value) > 0 and
    not any([for (delimiter in [";","{","}","\n"]) contains(value,delimiter)])
pub fn hex_color(value) bool => if (value is string) len(value) == 7 and starts_with(value,"#") and
    all([for (i in 1 to 6) (contains("0123456789abcdef",lower(slice(value,i,i+1))) or false)]) else false
pub fn validate_colors(props, fields, owner) bool^ {
    let invalid = [for (field in fields where props[field] != null and not css_color(props[field])) field];
    if (len(invalid) > 0) raise fail(owner,invalid[0] ++ " must be a single CSS color value") else true
}
pub fn boolean_props(props, keys, owner, family = "dtna") bool^ {
    let bad = [for (key in keys where has(props,key) and not (props[key] is bool)) key];
    if (len(bad) > 0) raise fail(owner, bad[0] ++ " must be bool",family) else true
}
pub fn select_options(props, family = "dtna") bool^ {
    if (not (props.options is array)) raise fail("select","options must be an array",family)
    else (
        let bad = [for (option in props.options where not (option is map) or
            not (option.value is string or option.value is symbol or option.value is int or option.value is bool) or
            (option.disabled != null and not (option.disabled is bool))) option],
        let names = [for (option in props.options) text(option.value)],
        if (len(bad) > 0) raise fail("select","options need scalar values and boolean disabled flags",family)
        else if (len(unique(names)) != len(names)) raise fail("select","option values must have distinct string representations",family)
        else true
    )
}
pub fn validate(kind, props) bool^ {
    // button owns its bool-or-delay loading contract; the other components use bool.
    let booleans = ["disabled", "readonly", "required", "checked", "default_checked", "wrap", "closable", "dot", "show_info", "danger", "block",
        *(if (kind == 'button') [] else ["loading"])]
    let valid = boolean_props(props, booleans, kind)^;
    // avatar validates numeric and responsive sizing before entering the shared node constructor.
    if (kind != 'avatar' and not enum_valid(props.size, ["small", "middle", "large"])^) raise fail(kind, "invalid size")
    else if (not enum_valid(props.status, if (kind == 'steps') ["wait","process","finish","error"]
        else if (kind == 'progress') ["normal","active","success","exception","error"]
        else if (kind == 'badge' or kind == 'tag') ["success","processing","default","error","warning","info"]
        else if (kind == 'result') ["success","error","warning","info","403","404","500"]
        else ["success", "warning", "error", "info"])^) raise fail(kind, "invalid status")
    else if (not enum_valid(props.variant, if (kind == 'tag') ["outlined","filled","solid"]
        else if (kind == 'alert') ["outlined","filled"]
        else ["primary", "default", "dashed", "text", "link"])^) raise fail(kind, "invalid variant")
    else if (has(props,'value') and has(props,'default_value')) raise fail(kind, "value and default_value are mutually exclusive")
    else if (has(props,'checked') and has(props,'default_checked')) raise fail(kind, "checked and default_checked are mutually exclusive")
    else true
}

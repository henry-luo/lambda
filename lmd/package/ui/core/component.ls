// UI descriptors retain source identity; presentation never owns application data (S12.1.3).
pub let common_props = ["id", "class", "style", "label", "title", "role", "tabindex"]

pub fn fail(kind, detail) error => error("dtna: " ++ string(kind) ++ ": " ++ detail)
pub fn option(props, key, fallback) => if (contains(props, symbol(key))) props[key] else fallback
pub fn text(value) string => if (value == null) "" else string(value)
pub fn px(value) => string(value) ++ "px"
pub fn boolean_attr(key, enabled) => if (enabled == true) {[key]:""} else {}
pub fn aria(value) => if (value == true) "true" else "false"
pub fn children(value) => if (value is array) value else if (value == null) [] else [value]
pub fn render(value) => if (value is array) [for (child in value) render(child)] else if (value is element) apply(value) else value
pub fn contents(node) => [for (child in content(node)) render(child)]

pub fn properties(props, allowed: array, kind, attributes = false) bool^ {
    if (not (props is map)) raise fail(kind,"props must be a map")
    else (
        let invalid = [for (key, value in props where not contains(allowed,string(key)) and
            not (attributes and (starts_with(string(key),"aria-") or starts_with(string(key),"data-")))) string(key)],
        if (len(invalid) > 0) raise fail(kind,"unsupported prop " ++ invalid[0]) else true
    )
}
pub fn node(kind, props, child, allowed = []) element^ {
    let names = properties(props,[*common_props,*allowed],kind,true)^
    let values = validate(kind,props)^;
    <dtna kind:kind,props:props,*children(child)>
}

pub fn attrs(props) => map([for (key, value in props where contains(["id", "role", "tabindex"], string(key)) or (string(key) == "title" and value is string) or
    starts_with(string(key), "aria-") or starts_with(string(key), "data-")) (key, value)])
pub fn classes(kind, props, extra = "") => "dtna-" ++ string(kind) ++
    (if (props.class == null) "" else " " ++ text(props.class)) ++
    (if (props.size == null) "" else " dtna-size-" ++ text(props.size)) ++
    (if (props.status == null) "" else " dtna-status-" ++ text(props.status)) ++
    (if (props.variant == null) "" else " dtna-variant-" ++ text(props.variant)) ++
    (if (extra == "") "" else " " ++ extra)
pub fn action(node, action, value = null) => {component:string(node.kind), id:node.props.id, action:action, value:value}

pub fn enum_valid(value, choices: array) bool^ => value == null or contains(choices, text(value))^
pub fn finite(value) => value is number and not (value is nan) and not (value is inf) and value != -inf
pub fn validate(kind, props) bool^ {
    let booleans = ["disabled", "loading", "readonly", "required", "checked", "default_checked", "wrap", "closable", "dot", "show_info", "danger", "block"]
    let bad_bool = [for (key in booleans where contains(props, symbol(key)) and not (props[key] is bool)) key];
    if (len(bad_bool) > 0) raise fail(kind, bad_bool[0] ++ " must be bool")
    else if (not enum_valid(props.size, ["small", "middle", "large"])^) raise fail(kind, "invalid size")
    else if (not enum_valid(props.status, ["success", "warning", "error", "info"])^) raise fail(kind, "invalid status")
    else if (not enum_valid(props.variant, ["primary", "default", "dashed", "text", "link"])^) raise fail(kind, "invalid variant")
    else if (contains(props, 'value') and contains(props, 'default_value')) raise fail(kind, "value and default_value are mutually exclusive")
    else if (contains(props, 'checked') and contains(props, 'default_checked')) raise fail(kind, "checked and default_checked are mutually exclusive")
    else true
}

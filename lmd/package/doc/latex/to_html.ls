// latex/to_html.ls — Custom HTML serializer for Lambda elements
// Handles array children flattening, HTML escaping, void elements, and attributes.
// This bypasses format('html') which wraps arrays in ul/li tags.

import util: .util

// ============================================================
// Public API
// ============================================================

// serialize a Lambda element tree to an HTML string
pub fn to_html(node) {
    serialize(node)
}

// ============================================================
// Core serializer
// ============================================================

fn serialize(node) {
    if (node == null) { "" }
    else if (node is string) { escape_html(node) }
    else if (node is int or node is float) { string(node) }
    else if (node is element) { serialize_element(node) }
    else if (node is array or node is list) { serialize_sequence(node) }
    else { string(node) }
}

// balanced joins avoid retaining every growing prefix of large embedded resources.
fn serialize_sequence(items) {
    util.str_join([for (child in items) serialize(child)], "")
}

// ============================================================
// Element serialization
// ============================================================

fn serialize_element(el) {
    let tag = string(name(el))
    if (tag == "style") {
        // HTML style is raw text: entity-escaping '>' changes CSS selectors.
        let css = join(content(el), "") ^ { "" }
        "<style" ++ serialize_attrs(el) ++ ">" ++ replace(css, "<", "\\3c ") ++ "</style>"
    } else if (is_void_element(tag)) {
        "<" ++ tag ++ serialize_attrs(el) ++ ">"
    } else {
        let children_html = serialize_children(el)
        "<" ++ tag ++ serialize_attrs(el) ++ ">" ++ children_html ++ "</" ++ tag ++ ">"
    }
}

fn serialize_children(el) {
    serialize_sequence(content(el))
}

// ============================================================
// Attribute serialization
// ============================================================

fn serialize_attrs(el) {
    util.str_join([for (k:symbol, v in el) format_attr(k, v)], "")
}

fn format_attr(key, val) {
    if (val is bool) {
        if (val == true) { " " ++ key }
        else { "" }
    }
    else if (val == null) { "" }
    else { " " ++ key ++ "=\"" ++ escape_attr(string(val)) ++ "\"" }
}

// ============================================================
// HTML escaping
// ============================================================

fn escape_html(s) {
    let r1 = replace(s, "&", "&amp;")
    let r2 = replace(r1, "<", "&lt;")
    replace(r2, ">", "&gt;")
}

fn escape_attr(s) {
    let r1 = replace(s, "&", "&amp;")
    let r2 = replace(r1, "<", "&lt;")
    let r3 = replace(r2, ">", "&gt;")
    let r4 = replace(r3, "\"", "&quot;")
    r4
}

// ============================================================
// Void elements (self-closing in HTML)
// ============================================================

fn is_void_element(tag) {
    match tag {
        case "br": true
        case "hr": true
        case "img": true
        case "input": true
        case "meta": true
        case "link": true
        case "col": true
        case "embed": true
        case "source": true
        case "area": true
        case "base": true
        case "track": true
        case "wbr": true
        default: false
    }
}

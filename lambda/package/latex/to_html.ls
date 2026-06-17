// latex/to_html.ls — Custom HTML serializer for Lambda elements
// Handles array children flattening, HTML escaping, void elements, and attributes.
// This bypasses format('html') which wraps arrays in ul/li tags.

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
    else if (node is array) { serialize_array(node) }
    else if (node is list) { serialize_list(node) }
    else { string(node) }
}

fn serialize_array(arr) {
    let n = len(arr)
    if (n == 0) { "" }
    else { serialize_array_rec(arr, 0, n, "") }
}

fn serialize_array_rec(arr, i, n, acc) {
    if (i >= n) { acc }
    else {
        let child = arr[i]
        let s = serialize(child)
        serialize_array_rec(arr, i + 1, n, acc ++ s)
    }
}

fn serialize_list(lst) {
    let n = len(lst)
    if (n == 0) { "" }
    else { serialize_list_rec(lst, 0, n, "") }
}

fn serialize_list_rec(lst, i, n, acc) {
    if (i >= n) { acc }
    else {
        let child = lst[i]
        let s = serialize(child)
        serialize_list_rec(lst, i + 1, n, acc ++ s)
    }
}

// ============================================================
// Element serialization
// ============================================================

fn serialize_element(el) {
    let tag = string(name(el))
    if (is_void_element(tag)) {
        "<" ++ tag ++ serialize_attrs(el) ++ ">"
    } else {
        let children_html = serialize_children(el)
        "<" ++ tag ++ serialize_attrs(el) ++ ">" ++ children_html ++ "</" ++ tag ++ ">"
    }
}

fn serialize_children(el) {
    let n = len(el)
    if (n == 0) { "" }
    else { serialize_children_rec(el, 0, n, "") }
}

fn serialize_children_rec(el, i, n, acc) {
    if (i >= n) { acc }
    else {
        let child = el[i]
        let s = serialize(child)
        serialize_children_rec(el, i + 1, n, acc ++ s)
    }
}

// ============================================================
// Attribute serialization
// ============================================================

fn serialize_attrs(el) {
    let pairs = [for (k:symbol, v in el) {key: k, val: v}]
    if (len(pairs) == 0) { "" }
    else { serialize_attrs_rec(pairs, 0, len(pairs), "") }
}

fn serialize_attrs_rec(pairs, i, n, acc) {
    if (i >= n) { acc }
    else {
        let pair = pairs[i]
        let attr_str = format_attr(pair.key, pair.val)
        serialize_attrs_rec(pairs, i + 1, n, acc ++ attr_str)
    }
}

fn format_attr(key, val) {
    if (string(key) == "math_data_attrs") {
        format_data_attrs(val)
    }
    else if (val is bool) {
        if (val == true) { " " ++ key }
        else { "" }
    }
    else if (val == null) { "" }
    else { " " ++ key ++ "=\"" ++ escape_attr(string(val)) ++ "\"" }
}

fn format_data_attrs(attrs) {
    if (attrs == null) ""
    else format_data_attrs_rec(attrs, 0, len(attrs), "")
}

fn format_data_attrs_rec(attrs, i, n, acc) {
    if (i >= n) acc
    else
        (let attr = attrs[i],
         let s = if (attr.has_value == true)
            " " ++ attr.name ++ "=\"" ++ escape_attr(string(attr.value)) ++ "\""
         else
            " " ++ attr.name ++ " ",
         format_data_attrs_rec(attrs, i + 1, n, acc ++ s))
}

// ============================================================
// HTML escaping
// ============================================================

fn escape_html(s) {
    // HTML5 only requires `<` and `&` to be escaped in text content; literal
    // `>` is allowed. MathLive's output emits `>` unescaped, so match it.
    let r1 = replace(s, "&", "&amp;")
    let r2 = replace(r1, "<", "&lt;")
    r2
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

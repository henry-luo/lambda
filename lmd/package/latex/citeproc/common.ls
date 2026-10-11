// Shared value and XML helpers; CSL remains data under S1.8.
pub let NS = "http://purl.org/net/xbiblio/csl"
let ERROR_CODES = ["invalid-csl-style", "invalid-csl-namespace", "unsupported-csl-element",
    "unsupported-csl-attribute", "csl-depth", "missing-csl-macro", "cyclic-csl-macro",
    "duplicate-csl-macro", "invalid-csl-layout", "invalid-csl-class", "cyclic-csl-parent",
    "missing-csl-parent", "invalid-csl-locale", "invalid-csl-json", "invalid-csl-item",
    "duplicate-csl-id", "unsupported-csl-feature", "invalid-csl-attribute"]

pub fn get(value, key, fallback = null) {
    let found = if (value == null) null else value[key]
    if (found == null) fallback else found
}

pub fn words(value) => if (value == null) [] else
    [for (part in split(replace(replace(string(value), "\n", " "), "\t", " "), " ")
        where part != "") part]

pub fn has(values, value) => any([for (item in values) item == value])

pub fn text(value) {
    if (value == null) ""
    else if (value is string) value
    else if (value is element) text(content(value))
    else if (value is array or value is list)
        join([for (child in value) text(child)], "")
    else if (value is map and value.csl_literal != null) text(value.csl_literal)
    else string(value)
}

pub fn tag(node) {
    if (not (node is element)) ""
    else {
        let parts = split(string(name(node)), ":")
        parts[len(parts) - 1]
    }
}

pub fn namespace_uri(node, bindings = {}) {
    let parts = split(string(name(node)), ":")
    let key = if (len(parts) == 1) "xmlns" else "xmlns:" ++ parts[0]
    get(map(node), key, bindings[key])
}

pub fn children(node, wanted = null) => if (not (node is element)) [] else [for (child in content(node)
    where child is element and not starts_with(string(name(child)), "!") and
        not starts_with(string(name(child)), "?") and
        (wanted == null or tag(child) == wanted)) child]

pub fn child(node, wanted) {
    let found = children(node, wanted)
    if (len(found) == 0) null else found[0]
}

pub fn attrs(node) => if (node is element) map(node) else if (node is map) node else {}

pub fn dictionary(pairs, index = 0, result = {}) {
    if (index >= len(pairs)) result
    else dictionary(pairs, index + 1,
        {*:result, [pairs[index].key]: pairs[index].value})
}

pub fn root(value) {
    if (tag(value) == "document") {
        let found = [for (child in content(value) where child is element and
            not starts_with(string(name(child)), "?") and
            not starts_with(string(name(child)), "!")) child]
        if (len(found) == 1) found[0] else null
    } else value
}

pub fn issue(code, message, source = null, item = null) map =>
    {code: code, message: message, package: "citeproc", file: source,
        item: item, offset: 0, line: 1}

pub fn failure(code, message, source = null) error {
    let found = [for (i, key in ERROR_CODES where key == code) i]
    error({code: if (len(found) == 0) 318 else 7000 + found[0],
        message: message, file: source})
}

pub fn error_issue(err: error, source = null) map =>
    {*:issue("csl-error", ""),
        code: if (err.code >= 7000 and err.code < 7000 + len(ERROR_CODES))
            ERROR_CODES[err.code - 7000] else "csl-error",
        message: err.message, file: if (err.file == null) source else err.file}

pub fn as_int(value, fallback = 0) => if (value == null) fallback
    else int(value) ^ { fallback }

pub fn truth(value) => value == true or value == "true"

pub fn nonempty(value) => value != null and text(value) != ""

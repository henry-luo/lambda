// shared value helpers; compiled scenes never borrow mutable DOM state.
pub fn value(v, fallback) => if (v == null) fallback else v
pub fn as_text(v) any => if (v == null) "" else (string(v) or "")
pub fn finite(v) => (v is int or v is float) and not (v is nan) and not (v is inf) and v != -inf
pub fn children(node, tag) => [for (c in content(node) where c is element and name(c) == tag) c]
pub fn objects(node) => [for (c in content(node) where c is element and contains(['text', 'image', 'shape', 'group', 'content'], name(c))) c]
pub fn num(node, key, fallback) => value(node[key], fallback)
pub fn fmt(v) => string(round(v * 1000000.0) / 1000000.0)
pub fn px(v) => fmt(v) ++ "px"
pub fn node_id(node, generated) => as_text(value(node.id, generated))
pub fn text_id(v) => (v is string or v is symbol) and len(as_text(v)) > 0
pub fn instance_id(v) => v is string and len(v) > 0 and all([for (i in 0 to len(v) - 1)
    contains("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-", v[i])])
pub fn fail(path, message) error => error("slide: " ++ path ++ ": " ++ message)

pub fn attributes(node, allowed, path) bool^ {
    let invalid = [for (key, v in node where (key is symbol or key is string) and not contains(allowed, string(key))) string(key)]
    if (len(invalid) > 0) raise fail(path, "unsupported attribute " ++ invalid[0])
    else true
}

pub fn numbers(node, keys, positive, path) bool^ {
    let invalid = [for (key in keys where node[key] != null and
        (not finite(node[key]) or (positive and node[key] <= 0.0))) key]
    if (len(invalid) > 0) raise fail(path, "invalid numeric " ++ invalid[0])
    else true
}

pub fn check_unique(items, path) bool^ {
    let ids = [for (item in items) item.id]
    let duplicated = [for (id in ids where len([for (other in ids where other == id) other]) > 1) id]
    if (len(duplicated) > 0) raise fail(path, "duplicate ID " ++ duplicated[0])
    else true
}

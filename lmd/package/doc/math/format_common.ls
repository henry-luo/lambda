// Shared scalar/array traversal for source writers (S2.6.1v2).
import util: .util

pub fn text(value, fallback = "") => if (value is string) value else fallback
pub fn is_tag(value, kind) => value is element and string(name(value)) == kind
pub fn item(value, depth, element_fn, symbol_fn) {
    if (depth > 50) "..."
    else if (value is element) element_fn(value, depth)
    else if (value is array) join([for (child in value) item(child, depth+1, element_fn, symbol_fn)], " ")
    else if (value is string) value
    else if (value is symbol) symbol_fn(string(value))
    else if (value is number) string(value)
    else ""
}

pub fn children(node, depth, item_fn, separator = " ") =>
    join([for (child in util.content_items(node)) item_fn(child, depth+1)], separator)

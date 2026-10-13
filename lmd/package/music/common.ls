// Shared XML axes and package diagnostics; attributes never enter the child axis (S8.1.2v2).
pub fn children(node) => if (node is element) [for (x in content(node) where x is element and
    slice(string(name(x)), 0, 1) != "!" and slice(string(name(x)), 0, 1) != "?") x] else []
pub fn named(node, tag) => [for (x in children(node) where string(name(x)) == tag) x]
pub fn child(node, tag) => named(node, tag)[0]
pub fn text(node) => if (node is string) node else if (node is element)
    join([for (x in content(node)) text(x)], "") else ""
pub fn field(node, tag, fallback = null) => (
    let found = child(node, tag), if (found == null) fallback else trim(text(found)))
pub fn field_number(node, tag, fallback = null) => (
    let value = field(node, tag), if (value == null) fallback else float(value))
pub fn present(node, tag) => child(node, tag) != null
pub fn fold(items, initial, step) => reduce([initial, *items], step)
pub fn finite(n) => n is number and n == n and abs(n) != inf
pub fn is_integer(n) => finite(n) and n == floor(n)
pub fn diagnostic(category, message, source = null) => {severity: "warning", category: category,
    message: "music: " ++ message, source: source}
pub fn require(ok, message) bool^ => if (ok) true else raise error("music: " ++ message)
pub fn only(node, allowed, location) bool^ => require(len(names(node,allowed,location))==0,
    location++": unsupported structural content")^
pub fn namespace_check(node, depth = 0) bool^ {
    let valid=require(depth<=128 and (node.xmlns==null or node.xmlns==""),"unsupported XML namespace or excessive tree depth")^
    all([for (child in children(node)) namespace_check(child,depth+1)^])
}
pub fn option_warnings(options, allowed) array^ {
    let checked=require(options is map and (options.strict==null or options.strict is bool),"expected options map and boolean strict flag")^;
    [for (k,v in options where not contains(allowed,string(k))) diagnostic("option","unknown option "++string(k))]
}
pub fn names(node, allowed, location) array => [for (x in children(node) where not contains(allowed, string(name(x))))
    diagnostic("unsupported-decoration", "unsupported " ++ string(name(x)), location)]
pub fn warnings(items, options) array^ {
    let checked = require(options.strict != true or len(items) == 0,
        "strict: " ++ string(items[0].message))^
    items
}

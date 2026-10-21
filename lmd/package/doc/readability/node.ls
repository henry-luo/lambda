// Readability's DOM model (vibe/Lambda_Pkg_Readability.md §3).
// Element literals cannot take a run-time tag, so the port runs on immutable
// node records and serializes the article itself (RD2). A node is a text
// string, a comment {tag: "#comment", data} or an element
// {tag, attrs: [[name, value]], kids, text}; text caches the textContent.

import .text

let PHRASING = ["abbr","audio","b","bdo","br","button","cite","code","data","datalist","dfn","em","embed",
    "i","img","input","kbd","label","mark","math","meter","noscript","object","output","progress","q","ruby",
    "samp","script","select","small","span","strong","sub","sup","textarea","time","var","wbr"]
let VOID = ["area","base","br","col","embed","hr","img","input","link","meta","source","track","wbr",
    "basefont","bgsound","frame","keygen","param"]
let RAW_TEXT = ["style","script","xmp","iframe","noembed","noframes","plaintext"]

// ---- construction ----
fn text_of(kids) => join([for (k in kids) if (k is string) k else k.text], "")
pub fn mk(tag, attrs: any, kids: any) => {tag: tag, attrs: attrs, kids: kids, text: text_of(kids)}
pub fn with_kids(n, kids: any) => {*: n, kids: kids, text: text_of(kids)}
pub fn retag(n, tag) => {*: n, tag: tag}
pub fn comment(data) => {tag: "#comment", data: data, attrs: [], kids: [], text: ""}

// a parsed Lambda HTML tree as records; doctype and processing nodes drop out
pub fn of(el) {
    if (el is string) el
    else if (name(el) == '#comment') comment(if (el.data is string) el.data else "")
    else mk(string(name(el)), [for (k, v at map(el)) [string(k), if (v is string) v else ""]],
        [for (c in content(el) where (c is string and c != "") or (c is element and is_node(c))) of(c)])
}
fn is_node(el) {
    let tag = string(name(el));
    tag == "#comment" or not (starts_with(tag, "#") or starts_with(tag, "?") or starts_with(tag, "!"))
}

// ---- element access (DOM getAttribute / children / className / id) ----
pub fn is_el(k) => k is map and k.tag != "#comment"
pub fn elems(n) => [for (k in n.kids where is_el(k)) k]
pub fn attr(n, key) string? {
    let hits = [for (a in n.attrs where a[0] == key) a[1]]
    let value = if (len(hits) == 0) null else hits[0];
    if (value is string) value else null
}
pub fn set_attr(n, key: any, value: any) => {*: n, attrs: if (attr(n, key) == null) [*n.attrs, [key, value]]
    else [for (a in n.attrs) if (a[0] == key) [key, value] else a]}
pub fn drop_attrs(n, names) => {*: n, attrs: [for (a in n.attrs where not (a[0] in names)) a]}
pub fn class_name(n) string => attr(n, "class") or ""
pub fn id_of(n) string => attr(n, "id") or ""
pub fn match_string(n) string => class_name(n) ++ " " ++ id_of(n)

// descendants with one of the tags, in document order (getElementsByTagName)
pub fn find_all(n, tags) => [for (k in n.kids where is_el(k))
    for (d in [k, *find_all(k, tags)] where d.tag in tags) d]
pub fn count_tags(n, tags) => len(find_all(n, tags))
pub fn first(n, tags) {
    let found = find_all(n, tags);
    if (len(found) == 0) null else found[0]
}
// every element of a tree, the root included, in document order
pub fn all_elements(n) => [n, *[for (k in n.kids where is_el(k)) for (d in all_elements(k)) d]]

// _hasAncestorTag over the ancestors, nearest first; max_depth <= 0 is unbounded
pub fn has_ancestor(anc, tag, max_depth = 3, pred = null) => any([for (i, a in anc
    where (max_depth <= 0 or i <= max_depth) and a.tag == tag and (pred == null or pred(a))) true])

// ---- tree transforms ----
// _removeNodes over getAllNodesWithTag: last first, so a node is judged after its
// descendants; judge(node, ancestors nearest first) returns true to remove
pub fn prune(e, tags, judge, anc = []) => with_kids(e, [for (k in e.kids,
    let r = if (is_el(k)) prune(k, tags, judge, [e, *anc]) else k
    where not (is_el(r) and r.tag in tags and judge(r, [e, *anc]))) r])
pub fn strip_tags(e, tags) => prune(e, tags, (k, anc) => true)
// _replaceNodeTags over every descendant with tag `from`
pub fn retag_all(e, from, to) => with_kids(e, [for (k in e.kids)
    if (is_el(k)) (let r = retag_all(k, from, to), if (r.tag == from) retag(r, to) else r) else k])
// _nextNode: the first sibling from i that is not white-space text or comment, skipping indices in skip
pub fn next_node(kids, i, skip = []) {
    if (i >= len(kids)) null
    else if (i in skip or (not is_el(kids[i]) and is_blank(if (kids[i] is string) kids[i] else kids[i].data)))
        next_node(kids, i + 1, skip)
    else i
}

// ---- text (Readability's textContent, trim and _getInnerText) ----
// String.prototype.trim: the built-in trim knows only ASCII white space
pub fn trim_text(s: string) string {
    let t = trim(s);
    if (t is \(ws ...) or t is \(... ws)) {
        let head = find(t, \(!ws), {limit: 1});
        if (len(head) == 0) "" else slice(t, head[0].index, find(t, \(!ws), {last: 1})[0].index + 1)
    } else t
}
pub fn squash(s: string) string => replace(s, \(ws{2+}), " ") ^ { s }
pub fn is_blank(s: string) bool => trim_text(s) == ""
// a node's cached textContent, typed for the text helpers
pub fn content_text(n) string => if (n is string) n else if (n.text is string) n.text else ""
pub fn inner_text(n, normalized = true) string {
    let t = trim_text(content_text(n));
    if (normalized) squash(t) else t
}
// ECMAScript String length counts UTF-16 code units
pub fn js_len(s: string) int => len(s) + len(find(s, \("\u{10000}" to "\u{10FFFF}")))
pub fn text_len(n) int => js_len(inner_text(n))
// text node or <br> (_isWhitespace); comments are neither
pub fn is_whitespace(k) => if (k is string) is_blank(k) else k.tag == "br"
// /\S$/: Readability's "has content" looks at the last character only
fn has_content(s) => s is \(... !ws)
// _textSimilarity: the share of b's tokens that also occur in a, by length
pub fn similarity(a, b) {
    let ta = tokens(a)
    let tb = tokens(b);
    if (len(ta) == 0 or len(tb) == 0) 0 else
        1 - len(join([for (t in tb where not (t in ta)) t], " ")) / len(join(tb, " "))
}
fn tokens(s) => [for (t in split(lower(s), \(!"\w"+)) where t != "") t]
// RegExp.test with the i flag
pub fn test(s: any, pattern) bool => s is string and len(find(s, pattern, {ignore_case: true, limit: 1})) > 0
pub fn image_ext(s: any) bool => test(s, \("." ("jpg" | "jpeg" | "png" | "webp")))

// ---- candidate classes (shared with Readability-readerable.js) ----
type unlikely = \("-ad-" | "ai2html" | "banner" | "breadcrumbs" | "combx" | "comment" | "community" |
    "cover-wrap" | "disqus" | "extra" | "footer" | "gdpr" | "header" | "legends" | "menu" | "related" |
    "remark" | "replies" | "rss" | "shoutbox" | "sidebar" | "skyscraper" | "social" | "sponsor" |
    "supplemental" | "ad-break" | "agegate" | "pagination" | "pager" | "popup" | "yom-remote")
type maybe_candidate = \("and" | "article" | "body" | "column" | "content" | "main" | "mathjax" | "shadow")
pub fn unlikely_candidate(ms) => test(ms, unlikely) and not test(ms, maybe_candidate)

// ---- class/id weight (_getClassWeight) ----
type positive = \("article" | "body" | "content" | "entry" | "hentry" | "h-entry" | "main" | "page" |
    "pagination" | "post" | "text" | "blog" | "story")
type negative = \("-ad-" | "hidden" | "banner" | "combx" | "comment" | "com-" | "contact" | "footer" | "gdpr" |
    "masthead" | "media" | "meta" | "outbrain" | "promo" | "related" | "scroll" | "share" | "shoutbox" |
    "sidebar" | "skyscraper" | "sponsor" | "shopping" | "tags" | "widget")
// the regex's ^hid$| hid$| hid |^hid alternatives are a space-delimited "hid"
fn side_weight(s) => if (s == "") 0 else
    (if (test(s, negative) or test(" " ++ s ++ " ", " hid ")) -25 else 0) + (if (test(s, positive)) 25 else 0)
pub fn class_weight(e, weigh) => if (weigh) side_weight(class_name(e)) + side_weight(id_of(e)) else 0

// ---- shape predicates ----
pub fn is_phrasing(k) => k is string or (is_el(k) and (k.tag in PHRASING or
    (k.tag in ["a","del","ins"] and all([for (c in k.kids) is_phrasing(c)]))))
pub fn has_single_tag(n, tag) {
    let kids = elems(n);
    len(kids) == 1 and kids[0].tag == tag and not any([for (k in n.kids where k is string) has_content(k)])
}
pub fn without_content(n) {
    let kids = elems(n);
    is_blank(n.text) and (len(kids) == 0 or len(kids) == count_tags(n, ["br","hr"]))
}
pub fn link_density(n) {
    let total = text_len(n);
    if (total == 0) 0 else sum([for (a in find_all(n, ["a"]))
        text_len(a) * (if (hash_url(attr(a, "href"))) 0.3 else 1)]) / total
}
fn hash_url(href) => href is \("#" !("\n" | "\r" | "\u2028" | "\u2029") ...)

// inline style declarations, last one wins (node.style.<property>)
pub fn style(n, property) {
    let decls = [for (d in split(attr(n, "style") or "", ";"), let at = index_of(d, ":")
        where at != null and lower(trim_text(slice(d, 0, at))) == property)
        lower(trim_text(replace(slice(d, at + 1), "!important", "")))];
    if (len(decls) == 0) null else decls[len(decls) - 1]
}
// _isProbablyVisible; isProbablyReaderable's checker skips visibility
pub fn visible(n, check_visibility = true) => style(n, "display") != "none" and
    (not check_visibility or style(n, "visibility") != "hidden") and attr(n, "hidden") == null and
    (attr(n, "aria-hidden") != "true" or contains(class_name(n), "fallback-image"))

// ---- serialization (the HTML fragment serializing algorithm) ----
pub fn to_html(kids, raw = false) => join([for (k in kids) node_html(k, raw)], "")
fn node_html(k, raw) {
    if (k is string) (if (raw) k else escape_text(k))
    else if (k.tag == "#comment") "<!--" ++ k.data ++ "-->"
    else "<" ++ k.tag ++ join([for (a in k.attrs) " " ++ a[0] ++ "=\"" ++ escape_attr(a[1]) ++ "\""], "") ++ ">" ++
        (if (k.tag in VOID) "" else to_html(k.kids, k.tag in RAW_TEXT) ++ "</" ++ k.tag ++ ">")
}
fn escape_text(s) => replace(replace(replace(replace(s, "&", "&amp;"), "\u00A0", "&nbsp;"), "<", "&lt;"), ">", "&gt;")
fn escape_attr(s) => replace(replace(replace(s, "&", "&amp;"), "\u00A0", "&nbsp;"), "\"", "&quot;")

// replace each match with f(match), for regex callbacks
pub fn replace_with(s, pattern, f) {
    let found = find(s, pattern);
    if (len(found) == 0) s else {
        let ends = [0, *(found |> ~.index + len(~.value))];
        join([*[for (i, m in found) slice(s, ends[i], m.index) ++ f(m.value)], slice(s, ends[len(found)])], "")
    }
}

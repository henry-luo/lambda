// latex/util.ls — Shared utility functions for the LaTeX package

// ============================================================
// Text extraction
// ============================================================

// extract plain text content from a node (recursively)
pub fn text_of(node) {
    if (node == null) ""
    else if (node is string) node
    else if (node is int) string(node)
    else if (node is float) string(node)
    else if (node is symbol) string(node)
    else if (node is element) text_of_element(node)
    else string(node)
}

fn text_of_element(el) {
    let n = len(el)
    if (n == 0) ""
    else if (n == 1) text_of(el[0])
    else join_children_text(el, 0, n, "")
}

fn join_children_text(el, i, n, acc) {
    if (i >= n) acc
    else join_children_text(el, i + 1, n, acc ++ text_of(el[i]))
}

// extract text with common symbol commands resolved
pub fn rich_text_of(node) {
    if (node == null) { "" }
    else if (node is string) { node }
    else if (node is int) { string(node) }
    else if (node is float) { string(node) }
    else if (node is symbol) { string(node) }
    else if (node is element) { rich_text_of_element(node) }
    else { string(node) }
}

fn rich_text_of_element(el) {
    let resolved = resolve_symbol_cmd(el)
    if (resolved != null) { resolved }
    else {
        let n = len(el)
        if (n == 0) { "" }
        else { join_rich_children(el, 0, n, "") }
    }
}

fn resolve_symbol_cmd(el) {
    let tag = string(name(el))
    match tag {
        case "LaTeX": "LaTeX"
        case "TeX": "TeX"
        case "LaTeXe": "LaTeX2e"
        case "ldots": "\u2026"
        case "dots": "\u2026"
        case "copyright": "\u00A9"
        case "dag": "\u2020"
        case "ddag": "\u2021"
        case "ss": "\u00DF"
        case "ae": "\u00E6"
        case "AE": "\u00C6"
        case "oe": "\u0153"
        case "OE": "\u0152"
        case "aa": "\u00E5"
        case "AA": "\u00C5"
        case "o": "\u00F8"
        case "O": "\u00D8"
        default: null
    }
}

fn join_rich_children(el, i, n, acc) {
    if (i >= n) { acc }
    else { join_rich_children(el, i + 1, n, acc ++ rich_text_of(el[i])) }
}

// ============================================================
// String helpers
// ============================================================

// slugify a string for use as an HTML id (lowercase, replace spaces with -)
pub fn slugify(s) {
    let lower = lower(trim(s))
    slug_chars(chars(lower), 0, len(chars(lower)), "")
}

fn slug_chars(cs, i, n, acc) {
    if (i >= n) { acc }
    else {
        let c = cs[i]
        let next = if (c == " " or c == "\t" or c == "\n") "-"
            else if (c == "_") "-"
            else c
        slug_chars(cs, i + 1, n, acc ++ next)
    }
}

// repeat a string n times
pub fn str_repeat(s, n) {
    if (n <= 0) ""
    else if (n == 1) s
    else s ++ str_repeat(s, n - 1)
}

// join an array of strings with a separator
pub fn str_join(arr, sep) {
    let n = len(arr)
    if (n == 0) ""
    else if (n == 1) string(arr[0])
    else join_rec(arr, sep, 1, n, string(arr[0]))
}

fn join_rec(arr, sep, i, n, acc) {
    if (i >= n) acc
    else join_rec(arr, sep, i + 1, n, acc ++ sep ++ string(arr[i]))
}

// check if a value is a parbreak symbol
pub fn is_parbreak(node) {
    node is symbol and string(node) == "parbreak"
}

// check if a node is whitespace-only string
pub fn is_whitespace(node) {
    if (not (node is string)) false
    else trim(node) == ""
}

// trim leading and trailing whitespace from children of an element
// returns array of non-empty children
pub fn trim_children(children) {
    let n = len(children)
    if (n == 0) { [] }
    else {
        let start = find_first_non_ws(children, 0, n)
        let end_idx = find_last_non_ws(children, n - 1)
        if (start > end_idx) { [] }
        else { slice(children, start, end_idx + 1) }
    }
}

fn find_first_non_ws(arr, i, n) {
    if (i >= n) n
    else if (is_whitespace(arr[i])) find_first_non_ws(arr, i + 1, n)
    else i
}

fn find_last_non_ws(arr, i) {
    if (i < 0) { -1 }
    else if (is_whitespace(arr[i])) { find_last_non_ws(arr, i - 1) }
    else { i }
}

// ============================================================
// Element helpers
// ============================================================

// get the first child element with a given tag, or null
pub fn find_child(el, tag_name) {
    let n = len(el)
    find_child_rec(el, tag_name, 0, n)
}

fn find_child_rec(el, tag_name, i, n) {
    if (i >= n) { null }
    else {
        let child = el[i]
        if (child is element) {
            if (string(name(child)) == string(tag_name)) { child }
            else { find_child_rec(el, tag_name, i + 1, n) }
        } else {
            find_child_rec(el, tag_name, i + 1, n)
        }
    }
}

// get the first descendant element with a given tag (depth-first), or null
pub fn find_descendant(el, tag_name) {
    let n = len(el)
    find_desc_rec(el, tag_name, 0, n)
}

fn find_desc_rec(el, tag_name, i, n) {
    if (i >= n) { null }
    else {
        let child = el[i]
        if (child is element) {
            if (string(name(child)) == string(tag_name)) { child }
            else {
                let deep = find_descendant(child, tag_name)
                if (deep != null) { deep }
                else { find_desc_rec(el, tag_name, i + 1, n) }
            }
        } else {
            find_desc_rec(el, tag_name, i + 1, n)
        }
    }
}

// collect all children from an element as an array
pub fn children_array(el) {
    let n = len(el)
    if (n == 0) { [] }
    else { (for (i in 0 to (n - 1)) el[i]) }
}

// get attribute value or default
pub fn attr_or(el, attr_name, default_val) {
    let v = el[attr_name]
    if (v != null) v
    else default_val
}

// rebuild an element with new children (generic version for any tag)
// Note: Lambda elements require literal tag names, so this returns the
// original element when children are unchanged, or wraps in a generic container
pub fn rebuild_with_children(el, new_kids) {
    // if children haven't changed (same length), return original
    if (len(new_kids) == len(el)) el
    else <group; for c in new_kids { c }>
}

// get text content of the Nth child
pub fn text_of_child(el, idx) {
    if (idx >= len(el)) ""
    else get_child_text(el[idx])
}

fn get_child_text(child) {
    if (child is string) child
    else if (child is element) text_of(child)
    else string(child)
}

// ============================================================
// Key-value option parsing (for \includegraphics[key=val,...])
// ============================================================

// parse "key1=val1, key2=val2, flag" → {key1: "val1", key2: "val2", flag: "true"}
pub fn parse_kv_options(text) {
    if (text == null) { {} }
    else {
        let parts = split(trim(text), ",")
        let pairs = build_kv_pairs(parts, 0, len(parts), [])
        map(pairs)
    }
}

fn build_kv_pairs(parts, i, n, acc) {
    if (i >= n) { acc }
    else {
        let part = trim(parts[i])
        if (part == "") { build_kv_pairs(parts, i + 1, n, acc) }
        else {
            let eq_pos = index_of(part, "=")
            if (eq_pos < 0) {
                // flag without value: keepaspectratio → "true"
                build_kv_pairs(parts, i + 1, n, acc ++ [part, "true"])
            } else {
                let key = trim(slice(part, 0, eq_pos))
                let val = trim(slice(part, eq_pos + 1, len(part)))
                build_kv_pairs(parts, i + 1, n, acc ++ [key, val])
            }
        }
    }
}

// extract text from element children, skipping brack_group elements
pub fn text_of_skip_brack(el) {
    let n = len(el)
    join_skip_brack(el, 0, n, "")
}

fn join_skip_brack(el, i, n, acc) {
    if (i >= n) { acc }
    else {
        let child = el[i]
        if (child is element and string(name(child)) == "brack_group") {
            join_skip_brack(el, i + 1, n, acc)
        } else {
            join_skip_brack(el, i + 1, n, acc ++ text_of(child))
        }
    }
}

// ============================================================
// Entry list lookup (for dynamic key-value stores)
// ============================================================

// look up a key in an entry list [{key, val}, ...]
// returns the val of the most recently added matching entry, or null
pub fn lookup(entries, k) {
    if (entries == null or len(entries) == 0) { null }
    else { lookup_rev(entries, k, len(entries) - 1) }
}

fn lookup_rev(entries, k, i) {
    if (i < 0) { null }
    else if (string(entries[i].key) == string(k)) { entries[i].val }
    else { lookup_rev(entries, k, i - 1) }
}

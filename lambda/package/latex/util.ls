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
        if (child is element and name(child) == tag_name) child
        else find_child_rec(el, tag_name, i + 1, n)
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

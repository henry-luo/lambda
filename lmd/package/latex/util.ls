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
    else if (node is element or node is array) text_of_element(node)
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
    else if (node is array) { join_rich_children(node, 0, len(node), "") }
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
    slug_chars(lower, 0, len(lower), "")
}

fn slug_chars(s, i, n, acc) {
    if (i >= n) { acc }
    else {
        let c = s[i]
        let next = if (c == " " or c == "\t" or c == "\n") "-"
            else if (c == "_") "-"
            else c
        slug_chars(s, i + 1, n, acc ++ next)
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
    else join_rec(arr, sep, i + 1, n, acc ++ sep ++ (arr[i]))
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
    else { [for (i in 0 to (n - 1)) el[i]] }
}

// S2.6.1v2/S2.6.4: command string runs are array children, preserving positional arguments.
pub fn command_args(el) {
    let children = if (el is element) content(el) else el;
    [for (child in children, arg in (if (child is array) child else [child])) arg]
}

// get attribute value or default
pub fn attr_or(el, attr_name, default_val) {
    let v = el[attr_name]
    if (v != null) v
    else default_val
}

// get text content of the Nth child
pub fn text_of_child(el, idx) {
    let args = command_args(el)
    if (idx >= len(args)) ""
    else text_of(args[idx])
}

// ============================================================
// Key-value option parsing (for \includegraphics[key=val,...])
// ============================================================

// parse balanced comma-separated keys; grouped values may contain commas.
pub fn parse_kv_options(text) {
    if (text == null) { {} }
    else {
        let trimmed = trim(text)
        if (trimmed == "") {
            // empty option text is a present-but-empty option list, not absence.
            {}
        } else {
            let parts = split_top_level(trimmed, ",")
            let pairs = build_kv_pairs(parts, 0, len(parts), [])
            map(pairs)
        }
    }
}

fn build_kv_pairs(parts, i, n, acc) {
    if (i >= n) { acc }
    else {
        let part = trim(parts[i])
        if (part == "") { build_kv_pairs(parts, i + 1, n, acc) }
        else {
            let eq_pos = top_level_separator(part, "=")
            if (eq_pos == null) {
                // flag without value: keepaspectratio → "true"
                build_kv_pairs(parts, i + 1, n, acc ++ [part, "true"])
            } else {
                let key = trim(slice(part, 0, eq_pos))
                let val = unwrap_braces(trim(slice(part, eq_pos + 1, len(part))))
                build_kv_pairs(parts, i + 1, n, acc ++ [key, val])
            }
        }
    }
}

pub fn split_top_level(source, separator) {
    split_top_level_rec(source, separator, 0, 0, 0, 0, false, false, 0, [])
}

fn split_top_level_rec(s, sep, i, braces, brackets, parens, quoted, escaped, start, acc) {
    if (i >= len(s)) acc ++ [slice(s, start, len(s))]
    else {
        let ch = slice(s, i, i + 1)
        if (escaped) split_top_level_rec(s, sep, i + 1, braces, brackets, parens, quoted, false, start, acc)
        else if (ch == "\\") split_top_level_rec(s, sep, i + 1, braces, brackets, parens, quoted, true, start, acc)
        else if (ch == "\"") split_top_level_rec(s, sep, i + 1, braces, brackets, parens, not quoted, false, start, acc)
        else if (quoted) split_top_level_rec(s, sep, i + 1, braces, brackets, parens, quoted, false, start, acc)
        else if (ch == "{") split_top_level_rec(s, sep, i + 1, braces + 1, brackets, parens, quoted, false, start, acc)
        else if (ch == "}") split_top_level_rec(s, sep, i + 1, braces - 1, brackets, parens, quoted, false, start, acc)
        else if (ch == "[") split_top_level_rec(s, sep, i + 1, braces, brackets + 1, parens, quoted, false, start, acc)
        else if (ch == "]") split_top_level_rec(s, sep, i + 1, braces, brackets - 1, parens, quoted, false, start, acc)
        else if (ch == "(") split_top_level_rec(s, sep, i + 1, braces, brackets, parens + 1, quoted, false, start, acc)
        else if (ch == ")") split_top_level_rec(s, sep, i + 1, braces, brackets, parens - 1, quoted, false, start, acc)
        else if (ch == sep and braces == 0 and brackets == 0 and parens == 0)
            split_top_level_rec(s, sep, i + 1, braces, brackets, parens, quoted, false, i + 1, acc ++ [slice(s, start, i)])
        else split_top_level_rec(s, sep, i + 1, braces, brackets, parens, quoted, false, start, acc)
    }
}

pub fn top_level_separator(source, separator) {
    let parts = split_top_level(source, separator)
    if (len(parts) < 2) null else len(parts[0])
}

pub fn unwrap_braces(source) {
    if (len(source) >= 2 and starts_with(source, "{") and ends_with(source, "}"))
        slice(source, 1, len(source) - 1)
    else source
}

pub fn optional_raw(el) {
    let raw = raw_argument(el, "optional", 0)
    if (raw != null) raw
    else {
        let bracket = find_child(el, "brack_group")
        if (bracket != null) text_of(bracket) else null
    }
}

pub fn raw_argument(el, kind, ordinal) {
    let groups = el.argument_groups
    if (groups == null) null else raw_argument_at(groups, kind, ordinal, 0)
}

pub fn option_enabled(value) {
    value != null and value != "false" and value != "0"
}

pub fn unsupported_element(package, message, offset) {
    <span class: "latex-unsupported", 'data-latex-error': message,
        'data-latex-offset': offset, 'data-latex-package': package, message>
}

pub fn diagnostic(code, package, item, message, offset) map =>
    {code: code, package: package, item: item, message: message, offset: offset}

// TeX pt is 1/72.27 inch; CSS pt is 1/72 inch.
let DIMENSION_UNITS = [
    {suffix: "mm", factor: 1.0, css: "mm", pixels: 96.0 / 25.4},
    {suffix: "cm", factor: 1.0, css: "cm", pixels: 96.0 / 2.54},
    {suffix: "in", factor: 1.0, css: "in", pixels: 96.0},
    {suffix: "px", factor: 1.0, css: "px", pixels: 1.0},
    {suffix: "em", factor: 1.0, css: "em", pixels: null},
    {suffix: "ex", factor: 1.0, css: "ex", pixels: null},
    {suffix: "bp", factor: 1.0, css: "pt", pixels: 96.0 / 72.0},
    {suffix: "pt", factor: 72.0 / 72.27, css: "pt", pixels: 96.0 / 72.27},
    {suffix: "pc", factor: 12.0 * 72.0 / 72.27, css: "pt", pixels: 12.0 * 96.0 / 72.27}
]

pub fn css_dimension(raw) {
    let source = trim(raw)
    if (source == "0") "0"
    else {
        let relative = css_relative_dimension(source)
        if (relative != null) relative else css_dimension_at(source, 0)
    }
}

fn css_relative_dimension(source) {
    let names = ["\\linewidth", "\\textwidth", "\\columnwidth"]
    css_relative_at(source, names, 0)
}

fn css_relative_at(source, names, i) {
    if (i >= len(names)) null
    else if (ends_with(source, names[i])) {
        let prefix = trim(slice(source, 0, len(source) - len(names[i])))
        let factor = if (prefix == "") 1.0 else float(prefix) ^ { null }
        if (factor == null) null else string(factor * 100.0) ++ "%"
    } else css_relative_at(source, names, i + 1)
}

fn css_dimension_at(source, i) {
    if (i >= len(DIMENSION_UNITS)) null
    else {
        let unit = DIMENSION_UNITS[i]
        if (ends_with(source, unit.suffix)) {
            let digits = slice(source, 0, len(source) - len(unit.suffix))
            let value = float(digits) ^ { null }
            if (value == null) null else string(value * unit.factor) ++ unit.css
        } else css_dimension_at(source, i + 1)
    }
}

// Raster and vector clip paths currently parse px/% lengths; convert TeX absolute units once.
pub fn css_pixel_dimension(raw) {
    let source = trim(raw)
    if (source == "0") "0px" else css_pixel_dimension_at(source, 0)
}

fn css_pixel_dimension_at(source, i) {
    if (i >= len(DIMENSION_UNITS)) null
    else {
        let unit = DIMENSION_UNITS[i]
        if (ends_with(source, unit.suffix)) {
            let digits = slice(source, 0, len(source) - len(unit.suffix))
            let value = float(digits) ^ { null }
            if (value == null or unit.pixels == null) null
            else string(value * unit.pixels) ++ "px"
        } else css_pixel_dimension_at(source, i + 1)
    }
}

fn raw_argument_at(groups, kind, ordinal, i) {
    if (i >= len(groups)) null
    else if (groups[i].kind != kind) raw_argument_at(groups, kind, ordinal, i + 1)
    else if (ordinal == 0) groups[i].raw
    else raw_argument_at(groups, kind, ordinal - 1, i + 1)
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

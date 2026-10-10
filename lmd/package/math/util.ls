// math/util.ls — Numeric and string helpers for the math package

// ============================================================
// Number formatting
// ============================================================

// format a float to a fixed number of decimal places as string
pub fn fmt_num(x, decimals) {
    let factor = 10.0 ** decimals
    string(round(x * factor) / factor)
}

// format a number as em units: "0.5em".
// Keep five decimal places for SVG dimensions and baseline offsets.
pub fn fmt_em(x) {
    if (abs(x) >= 100000.0) fmt_large_em(x)
    else fmt_num(x, 5) ++ "em"
}

fn fmt_large_em(x) {
    let scaled = int(round(x * 10.0))
    let s = string(abs(scaled))
    let body = if (len(s) <= 1) "0." ++ s
        else slice(s, 0, len(s) - 1) ++ "." ++ slice(s, len(s) - 1, len(s));
    (if (scaled < 0) "-" else "") ++ body ++ "em"
}

// format a number as percentage: "70%"
pub fn fmt_pct(x) => fmt_num(x * 100.0, 1) ++ "%"

// ============================================================
// Math helpers
// ============================================================

// clamp a number to [lo, hi]
pub fn clamp(x, lo, hi) => max(lo, min(hi, x))

// ============================================================
// String helpers
// ============================================================

// repeat a string n times
pub fn str_repeat(s, n) {
    if (n <= 0) ""
    else if (n == 1) s
    else s ++ str_repeat(s, n - 1)
}

// join an array of strings with a separator
pub fn str_join(arr, sep) {
    if (len(arr) == 0) ""
    else if (len(arr) == 1) string(arr[0])
    else (arr[0]) ++ sep ++ str_join(slice(arr, 1, len(arr)), sep)
}

// check if string starts with a prefix
pub fn starts_with(s, prefix) {
    if (len(prefix) > len(s)) false
    else slice(s, 0, len(prefix)) == prefix
}

// ============================================================
// Element helpers
// ============================================================

// get element attribute with default
pub fn attr_or(el, key, default_val) {
    let val = el[key]
    if (val == null) default_val else val
}

// get text content of a leaf element
pub fn text_of(el) {
    if (el is string or el is symbol) string(el)
    else if (el is element or el is array) children_text(el, text_of)
    else if (el == null) ""
    else string(el)
}

// extract child text without interpreting attribute-count slots as glyphs.
pub fn children_text(node, text_fn) {
    let items = content_items(node)
    join_content_text(items, text_fn, 0, "")
}

fn join_content_text(items, text_fn, i, acc) {
    if (i >= len(items)) acc
    else join_content_text(items, text_fn, i + 1, acc ++ text_fn(items[i]))
}

// S2.6.1v2: array children retain the separate tokens of a math content run.
pub fn content_items(node) {
    let children = if (node is element) content(node) else node;
    [for (child in children, item in (if (child is array) child else [child])) item]
}

// Shared syntax parsing; callers resolve ex/absolute lengths in their font context.
pub fn dimension_from_string(raw) {
    let start = find_number_start(raw, 0)
    if (start >= len(raw)) {
        {value: 0.0, sign: 1.0, unit: "em"}
    } else {
        let end = find_number_end(raw, start)
        let unit_end = find_unit_end(raw, end)
        let num_text = slice(raw, start, end)
        let unit_text = if (unit_end > end) slice(raw, end, unit_end) else "em"
        let sign = if (len(num_text) > 0 and slice(num_text, 0, 1) == "-") -1.0 else 1.0
        let abs_start = if (len(num_text) > 0 and (slice(num_text, 0, 1) == "-" or slice(num_text, 0, 1) == "+")) 1 else 0
        let abs_text = slice(num_text, abs_start, len(num_text))
        {value: float(abs_text), sign: sign, unit: unit_text}
    }
}

fn find_number_start(s, i) {
    if (i >= len(s)) i
    else if (is_number_start_char(slice(s, i, i + 1))) i
    else find_number_start(s, i + 1)
}

fn find_number_end(s, i) {
    if (i >= len(s)) i
    else if (is_number_char(slice(s, i, i + 1))) find_number_end(s, i + 1)
    else i
}

fn find_unit_end(s, i) {
    if (i >= len(s)) i
    else if (is_unit_char(slice(s, i, i + 1))) find_unit_end(s, i + 1)
    else i
}

fn is_number_start_char(ch) {
    ch == "-" or ch == "+" or ch == "." or is_digit_char(ch)
}

fn is_number_char(ch) {
    ch == "-" or ch == "+" or ch == "." or is_digit_char(ch)
}

fn is_digit_char(ch) {
    ch == "0" or ch == "1" or ch == "2" or ch == "3" or ch == "4" or
    ch == "5" or ch == "6" or ch == "7" or ch == "8" or ch == "9"
}

fn is_unit_char(ch) {
    ch == "a" or ch == "b" or ch == "c" or ch == "d" or ch == "e" or
    ch == "f" or ch == "g" or ch == "h" or ch == "i" or ch == "j" or
    ch == "k" or ch == "l" or ch == "m" or ch == "n" or ch == "o" or
    ch == "p" or ch == "q" or ch == "r" or ch == "s" or ch == "t" or
    ch == "u" or ch == "v" or ch == "w" or ch == "x" or ch == "y" or
    ch == "z"
}


// Preserve explicit row/cell token boundaries during matrix layout.
pub fn parse_rows(body, i, n, rows, current_row, current_cell) {
    if (i >= n) {
        rows ++ [make_row(current_row ++ [make_cell(current_cell)])]
    } else if (body[i] == 'row_sep' or body[i] == 'col_sep' or
        (body[i] is element and name(body[i]) == 'row_sep')) {
        if (body[i] == 'row_sep' or (body[i] is element and name(body[i]) == 'row_sep'))
            parse_rows(body, i + 1, n, rows ++ [{*:make_row(current_row ++ [make_cell(current_cell)]), gap: body[i].gap}], [], [])
        else
            parse_rows(body, i + 1, n, rows, current_row ++ [make_cell(current_cell)], [])
    } else
        parse_rows(body, i + 1, n, rows, current_row, current_cell ++ [body[i]])
}

fn make_cell(items) => {items: items}

fn make_row(cells) => {cells: cells}

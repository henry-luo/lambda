// pdf/stream.ls — PDF content-stream tokenizer
//
// Converts the raw bytes of a PDF content stream (already decompressed by
// the C parser and exposed as `stream.data`) into a list of operator records:
//
//     [ { op: "BT",  operands: [] },
//       { op: "Tf",  operands: [ {kind: "name", value: "F1"}, 12.0 ] },
//       { op: "Tj",  operands: [ {kind: "string", value: "Hello"} ] },
//       { op: "ET",  operands: [] }, ... ]
//
// Operands are pushed onto a working stack as they are scanned; each time an
// operator (alphabetic token, or `*`/`'`/`"`) is read, the pending stack
// becomes that operator's operands and is reset.
//
// IMPORTANT: every helper uses explicit `return` because procedural functions
// (`pn`) do not implicitly yield the value of an if/else expression — only
// `return` actually carries a result back to the caller.

// ============================================================
// Character classification
// ============================================================

fn is_ws(c: string) {
    c == " " or c == "\t" or c == "\n" or c == "\r" or c == "\f"
}

fn is_digit_code(k) {
    (k >= 48) and (k <= 57)
}

fn is_alpha_code(k) {
    ((k >= 65) and (k <= 90)) or ((k >= 97) and (k <= 122))
}

fn is_op_char(c: string) {
    let k = ord(c);
    is_alpha_code(k) or c == "*" or c == "'" or c == "\""
}

fn is_num_start(c: string) {
    is_digit_code(ord(c)) or c == "+" or c == "-" or c == "."
}

fn is_hex_ws(c: string) {
    c == " " or c == "\t" or c == "\n" or c == "\r" or c == "\f"
}

fn is_hex_digit_code(k) {
    ((k >= 48) and (k <= 57)) or ((k >= 65) and (k <= 70)) or ((k >= 97) and (k <= 102))
}

fn is_octal_code(k) {
    (k >= 48) and (k <= 55)
}

fn octal_digit_value(c: string) {
    ord(c) - 48
}

// ============================================================
// String slicing (Lambda strings are char-indexable)
// ============================================================

fn slice_str(s: string, a: int, b: int) {
    slice(s, a, b)
}

// ============================================================
// Number / name / string parsing
// ============================================================

fn _read_number_pos(s: string, j: int, has_dot: bool) {
    if (j >= len(s)) { { end: j, has_dot: has_dot } }
    else {
        let c = s[j]
        if (c == ".") {
            if (has_dot) { { end: j, has_dot: has_dot } }
            else { _read_number_pos(s, j + 1, true) }
        }
        else if (is_digit_code(ord(c))) { _read_number_pos(s, j + 1, has_dot) }
        else { { end: j, has_dot: has_dot } }
    }
}

fn read_number(s: string, i: int) {
    let start = if (i < len(s) and (s[i] == "+" or s[i] == "-")) { i + 1 } else { i }
    let r = _read_number_pos(s, start, false)
    let j = r.end
    let raw = slice_str(s, i, j)
    if (r.has_dot) { { value: float(raw), end: j } }
    else { { value: int(raw), end: j } }
}

// Parse a name token "/Name". Caller positions i at the leading '/'.
fn _read_name_pos(s: string, j: int) {
    if (j >= len(s)) { j }
    else {
        let c = s[j]
        if (is_ws(c) or c == "/" or c == "[" or c == "]" or
            c == "(" or c == ")" or c == "<" or c == ">") { j }
        else { _read_name_pos(s, j + 1) }
    }
}

fn read_name(s: string, i: int) {
    let j = _read_name_pos(s, i + 1)
    { value: { kind: "name", value: slice_str(s, i + 1, j) }, end: j }
}

fn _clean_hex_loop(raw: string, k: int, out: string) {
    if (k >= len(raw)) { out }
    else {
        let c = raw[k]
        if ((not is_hex_ws(c)) and is_hex_digit_code(ord(c))) {
            _clean_hex_loop(raw, k + 1, out ++ c)
        }
        else { _clean_hex_loop(raw, k + 1, out) }
    }
}

fn clean_hex_string(raw: string) {
    _clean_hex_loop(raw, 0, slice_str(raw, 0, 0))
}

// Parse a literal string "(...)", balancing nested unescaped parens.
fn _read_octal_tail(s: string, j: int, oct: int, count: int) {
    if (count >= 3 or j >= len(s) or not is_octal_code(ord(s[j]))) {
        { value: oct, end: j }
    }
    else {
        _read_octal_tail(s, j + 1, (oct * 8) + octal_digit_value(s[j]), count + 1)
    }
}

fn _read_escape(s: string, j: int) {
    let esc = s[j]
    if (is_octal_code(ord(esc))) {
        let r = _read_octal_tail(s, j + 1, octal_digit_value(esc), 1)
        { trans: chr(r.value), end: r.end }
    }
    else {
        let trans = if (esc == "n") { "\n" }
            else if (esc == "r") { "\r" }
            else if (esc == "t") { "\t" }
            else if (esc == "b") { "\b" }
            else if (esc == "f") { "\f" }
            else if (esc == "(") { "(" }
            else if (esc == ")") { ")" }
            else if (esc == "\\") { "\\" }
            else { esc }
        { trans: trans, end: j + 1 }
    }
}

fn _lit_pre_parts(s: string, part_start: int, j: int, parts) {
    if (j > part_start) { parts ++ [slice(s, part_start, j)] }
    else { parts }
}

fn _lit_loop(s: string, j: int, depth: int, part_start: int, parts) {
    if (j >= len(s) or depth <= 0) {
        let final_parts = if (depth > 0 and j > part_start) { parts ++ [slice(s, part_start, j)] } else { parts }
        { parts: final_parts, end: j }
    }
    else {
        let c = s[j]
        if (c == "\\") {
            let pre = _lit_pre_parts(s, part_start, j, parts)
            let esc_i = j + 1
            if (esc_i >= len(s)) {
                { parts: pre, end: esc_i }
            }
            else {
                let e = _read_escape(s, esc_i)
                _lit_loop(s, e.end, depth, e.end, pre ++ [e.trans])
            }
        }
        else if (c == "(") {
            _lit_loop(s, j + 1, depth + 1, part_start, parts)
        }
        else if (c == ")") {
            let next_depth = depth - 1
            if (next_depth == 0) {
                { parts: _lit_pre_parts(s, part_start, j, parts), end: j + 1 }
            }
            else { _lit_loop(s, j + 1, next_depth, part_start, parts) }
        }
        else { _lit_loop(s, j + 1, depth, part_start, parts) }
    }
}

fn read_lit_string(s: string, i: int) {
    let r = _lit_loop(s, i + 1, 1, i + 1, [])
    { value: { kind: "string", value: r.parts | join("") }, end: r.end }
}

// Parse a hex string "<...>". Caller positions i at the opening '<'.
fn _find_char(s: string, j: int, target: string) {
    if (j >= len(s) or s[j] == target) { j }
    else { _find_char(s, j + 1, target) }
}

fn read_hex_string(s: string, i: int) {
    let j = _find_char(s, i + 1, ">")
    let raw = slice_str(s, i + 1, j)
    let endp = if (j < len(s)) { j + 1 } else { j }
    { value: { kind: "hex", value: clean_hex_string(raw) }, end: endp }
}

// Parse an alphabetic operator token ("BT", "Tf", "Tj", "TJ", "T*", etc.).
fn _read_op_pos(s: string, j: int) {
    if (j < len(s) and is_op_char(s[j])) { _read_op_pos(s, j + 1) }
    else { j }
}

fn read_op(s: string, i: int) {
    let j = _read_op_pos(s, i)
    { value: slice_str(s, i, j), end: j }
}

// Skip an inline dict "<<...>>" returning the position after the closing
// ">>". We don't try to parse the contents in Phase 2.
fn _skip_dict_pos(s: string, j: int) {
    if (j + 1 >= len(s)) { j }
    else if (s[j] == ">" and s[j + 1] == ">") { j + 2 }
    else { _skip_dict_pos(s, j + 1) }
}

fn skip_dict(s: string, i: int) {
    _skip_dict_pos(s, i + 2)
}

// Parse and skip a BI..ID..EI inline-image segment. `i` is positioned right
// after the "BI" operator token. Returns { end, info } so the caller can jump
// past the binary payload, which is otherwise unparseable as PDF tokens.
// The inline image dictionary is preserved as [{ key, value }, ...] pairs.
fn _skip_ws(s: string, j: int, n: int) {
    if (j < n and is_ws(s[j])) { _skip_ws(s, j + 1, n) }
    else { j }
}

fn _skip_comment(s: string, j: int, n: int) {
    if (j < n and s[j] != "\n" and s[j] != "\r") { _skip_comment(s, j + 1, n) }
    else { j }
}

fn _is_inline_id(s: string, j: int, n: int) {
    (j + 2 <= n and s[j] == "I" and s[j + 1] == "D"
        and (j + 2 == n or is_ws(s[j + 2])))
}

fn _is_inline_ei(s: string, k: int, n: int) {
    (is_ws(s[k]) and k + 3 <= n
        and s[k + 1] == "E" and s[k + 2] == "I"
        and (k + 3 == n or is_ws(s[k + 3]) or s[k + 3] == "/"
             or s[k + 3] == "<" or s[k + 3] == "("))
}

fn _find_inline_ei(s: string, k: int, n: int) {
    if (k + 2 > n) { n }
    else if (_is_inline_ei(s, k, n)) { k }
    else { _find_inline_ei(s, k + 1, n) }
}

fn _skip_inline_pairs(s: string, j0: int, n: int, pairs) {
    let j = _skip_ws(s, j0, n)
    if (j >= n) { { end: n, info: { kind: "inline_image", dict: pairs, data: "" } } }
    else if (_is_inline_id(s, j, n)) {
        let data_start = j + 3
        let k = _find_inline_ei(s, data_start, n)
        let endp = if (k >= n) { n } else { k + 3 }
        { end: endp, info: { kind: "inline_image", dict: pairs, data: slice_str(s, data_start, k) } }
    }
    else {
        let key = read_operand(s, j)
        if (key.value is map and key.value.kind == "name") {
            let val_start = _skip_ws(s, key.end, n)
            let val = read_operand(s, val_start)
            _skip_inline_pairs(s, val.end, n, pairs ++ [{ key: key.value.value, value: val.value }])
        }
        else { _skip_inline_pairs(s, j + 1, n, pairs) }
    }
}

fn skip_inline_image(s: string, i: int) {
    _skip_inline_pairs(s, i, int(len(s)), [])
}

// ============================================================
// Operand dispatcher
// ============================================================

fn read_operand(s: string, i: int) {
    if (i >= len(s)) { { value: null, end: i } }
    else {
        let c = s[i]
        if (c == "/") { read_name(s, i) }
        else if (c == "(") { read_lit_string(s, i) }
        else if (c == "[") { read_array(s, i) }
        else if (c == "<") {
        if (i + 1 < len(s) and s[i + 1] == "<") {
            let endp = skip_dict(s, i)
            { value: { kind: "dict", value: {} }, end: endp }
        }
        else { read_hex_string(s, i) }
        }
        else if (is_num_start(c)) { read_number(s, i) }
        else { { value: null, end: i + 1 } }
    }
}

// Parse "[ ... ]" as a list of operands (recursive for safety).
fn _read_array_items(s: string, j0: int, items) {
    let j = _skip_ws(s, j0, int(len(s)))
    if (j >= len(s)) { { items: items, end: j } }
    else if (s[j] == "]") { { items: items, end: j + 1 } }
    else {
        let r = read_operand(s, j)
        _read_array_items(s, r.end, items ++ [r.value])
    }
}

fn read_array(s: string, i: int) {
    let r = _read_array_items(s, i + 1, [])
    { value: { kind: "array", value: r.items }, end: r.end }
}

// ============================================================
// Public API
// ============================================================

// Tokenize a content-stream string into a flat list of operator records.
// Comments (lines starting with '%') are stripped.
fn _parse_content_loop(bytes: string, i: int, n: int, ops, stack) {
    if (i >= n) { ops }
    else {
        let c = bytes[i]

        // skip whitespace
        if (is_ws(c)) { _parse_content_loop(bytes, i + 1, n, ops, stack) }

        // skip a "%..." comment to end of line
        else if (c == "%") { _parse_content_loop(bytes, _skip_comment(bytes, i, n), n, ops, stack) }

        // operator token (must come before generic operand reading because
        // bare letters look like neither numbers nor delimiters)
        else if (is_op_char(c)) {
            let r = read_op(bytes, i)
            // Inline-image guard: BI starts a binary segment that is NOT
            // valid PDF tokens. Skip everything up to (and including) the
            // matching EI, and emit a synthetic `inline_image` op so the
            // interpreter can render a placeholder.
            if (r.value == "BI") {
                let inline = skip_inline_image(bytes, r.end)
                _parse_content_loop(bytes, inline.end, n,
                    ops ++ [{ op: "inline_image", operands: [inline.info] }], [])
            }
            else {
                let operands = (for (v in stack) v)
                _parse_content_loop(bytes, r.end, n,
                    ops ++ [{ op: r.value, operands: operands }], [])
            }
        }

        // otherwise a literal operand (number / name / string / hex / array)
        else {
            let r = read_operand(bytes, i)
            let next_stack = if (r.value != null) { stack ++ [r.value] } else { stack }
            _parse_content_loop(bytes, r.end, n, ops, next_stack)
        }
    }
}

pub fn parse_content_stream(bytes: string) {
    _parse_content_loop(bytes, 0, int(len(bytes)), [], [])
}

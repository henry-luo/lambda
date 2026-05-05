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

// ============================================================
// String slicing (Lambda strings are char-indexable)
// ============================================================

pn slice_str(s: string, a: int, b: int) {
    var out = ""
    var k = a
    while (k < b) {
        out = out ++ s[k]
        k = k + 1
    }
    return out
}

// ============================================================
// Number / name / string parsing
// ============================================================

pn read_number(s: string, i: int) {
    var j = i
    var has_dot = false
    if (j < len(s) and (s[j] == "+" or s[j] == "-")) { j = j + 1 }
    while (j < len(s)) {
        let c = s[j]
        if (c == ".") {
            if (has_dot) { break }
            has_dot = true
            j = j + 1
        }
        else if (is_digit_code(ord(c))) { j = j + 1 }
        else { break }
    }
    let raw = slice_str(s, i, j)
    if (has_dot) { return { value: float(raw), end: j } }
    return { value: int(raw), end: j }
}

// Parse a name token "/Name". Caller positions i at the leading '/'.
pn read_name(s: string, i: int) {
    var j = i + 1
    while (j < len(s)) {
        let c = s[j]
        if (is_ws(c) or c == "/" or c == "[" or c == "]" or
            c == "(" or c == "<" or c == ">") { break }
        j = j + 1
    }
    return { value: { kind: "name", value: slice_str(s, i + 1, j) }, end: j }
}

// Parse a literal string "(...)", balancing nested unescaped parens.
pn read_lit_string(s: string, i: int) {
    var j = i + 1
    var depth = 1
    var part_start = j
    var parts = []
    while (j < len(s) and depth > 0) {
        let c = s[j]
        if (c == "\\") {
            if (j > part_start) { parts = parts ++ [slice(s, part_start, j)] }
            j = j + 1
            if (j >= len(s)) { break }
            let esc = s[j]
            var trans = esc
            if (esc == "n")        { trans = "\n" }
            else if (esc == "r")   { trans = "\r" }
            else if (esc == "t")   { trans = "\t" }
            else if (esc == "b")   { trans = "\b" }
            else if (esc == "f")   { trans = "\f" }
            else if (esc == "(")   { trans = "(" }
            else if (esc == ")")   { trans = ")" }
            else if (esc == "\\")  { trans = "\\" }
            parts = parts ++ [trans]
            j = j + 1
            part_start = j
        }
        else if (c == "(") {
            depth = depth + 1
            j = j + 1
        }
        else if (c == ")") {
            depth = depth - 1
            if (depth == 0) {
                if (j > part_start) { parts = parts ++ [slice(s, part_start, j)] }
            }
            j = j + 1
        }
        else {
            j = j + 1
        }
    }
    if (depth > 0 and j > part_start) { parts = parts ++ [slice(s, part_start, j)] }
    let out = parts | join("")
    return { value: { kind: "string", value: out }, end: j }
}

// Parse a hex string "<...>". Caller positions i at the opening '<'.
pn read_hex_string(s: string, i: int) {
    var j = i + 1
    while (j < len(s) and s[j] != ">") { j = j + 1 }
    let raw = slice_str(s, i + 1, j)
    var endp = j
    if (j < len(s)) { endp = j + 1 }
    return { value: { kind: "hex", value: raw }, end: endp }
}

// Parse an alphabetic operator token ("BT", "Tf", "Tj", "TJ", "T*", etc.).
pn read_op(s: string, i: int) {
    var j = i
    while (j < len(s) and is_op_char(s[j])) { j = j + 1 }
    return { value: slice_str(s, i, j), end: j }
}

// Skip an inline dict "<<...>>" returning the position after the closing
// ">>". We don't try to parse the contents in Phase 2.
pn skip_dict(s: string, i: int) {
    var j = i + 2
    while (j + 1 < len(s)) {
        if (s[j] == ">" and s[j + 1] == ">") { return j + 2 }
        j = j + 1
    }
    return j
}

// Skip a BI..ID..EI inline-image segment. `i` is positioned right after
// the "BI" operator token. Returns { end: pos_after_EI } so the caller
// can jump past the binary payload, which is otherwise unparseable as
// PDF tokens. The dict between BI and ID is recorded as a `kind:"dict"`
// operand on the synthesized op.
pn skip_inline_image(s: string, i: int) {
    var j = i
    let n = len(s)
    // Scan for the "ID" token (whitespace-delimited).
    while (j < n) {
        // skip whitespace and then look for "ID" followed by ws/binary
        if (j + 2 <= n and s[j] == "I" and s[j + 1] == "D"
            and (j + 2 == n or is_ws(s[j + 2]))) {
            j = j + 3
            // After "ID" + one whitespace byte, the binary stream begins.
            // Scan ahead for "EI" preceded by whitespace.
            while (j + 2 <= n) {
                if (is_ws(s[j]) and j + 3 <= n
                    and s[j + 1] == "E" and s[j + 2] == "I"
                    and (j + 3 == n or is_ws(s[j + 3]) or s[j + 3] == "/"
                         or s[j + 3] == "<" or s[j + 3] == "(")) {
                    return j + 3
                }
                j = j + 1
            }
            return n
        }
        j = j + 1
    }
    return n
}

// ============================================================
// Operand dispatcher
// ============================================================

pn read_operand(s: string, i: int) {
    if (i >= len(s)) { return { value: null, end: i } }
    let c = s[i]
    if (c == "/")            { return read_name(s, i) }
    if (c == "(")            { return read_lit_string(s, i) }
    if (c == "[")            { return read_array(s, i) }
    if (c == "<") {
        if (i + 1 < len(s) and s[i + 1] == "<") {
            let endp = skip_dict(s, i)
            return { value: { kind: "dict", value: {} }, end: endp }
        }
        return read_hex_string(s, i)
    }
    if (is_num_start(c))     { return read_number(s, i) }
    return { value: null, end: i + 1 }
}

// Parse "[ ... ]" as a list of operands (recursive for safety).
pn read_array(s: string, i: int) {
    var j = i + 1
    var items = []
    while (j < len(s)) {
        while (j < len(s) and is_ws(s[j])) { j = j + 1 }
        if (j >= len(s)) { break }
        if (s[j] == "]") { j = j + 1; break }
        let r = read_operand(s, j)
        items = items ++ [r.value]
        j = r.end
    }
    return { value: { kind: "array", value: items }, end: j }
}

// ============================================================
// Public API
// ============================================================

// Tokenize a content-stream string into a flat list of operator records.
// Comments (lines starting with '%') are stripped.
pub pn parse_content_stream(bytes: string) {
    var ops = []
    var stack = []
    var i = 0
    let n = len(bytes)

    while (i < n) {
        let c = bytes[i]

        // skip whitespace
        if (is_ws(c)) { i = i + 1; continue }

        // skip a "%..." comment to end of line
        if (c == "%") {
            while (i < n and bytes[i] != "\n" and bytes[i] != "\r") {
                i = i + 1
            }
            continue
        }

        // operator token (must come before generic operand reading because
        // bare letters look like neither numbers nor delimiters)
        if (is_op_char(c)) {
            let r = read_op(bytes, i)
            // Inline-image guard: BI starts a binary segment that is NOT
            // valid PDF tokens. Skip everything up to (and including) the
            // matching EI, and emit a synthetic `inline_image` op so the
            // interpreter can render a placeholder.
            if (r.value == "BI") {
                let after_ei = skip_inline_image(bytes, r.end)
                let operands = (for (v in stack) v)
                ops = ops ++ [{ op: "inline_image", operands: operands }]
                stack = []
                i = after_ei
                continue
            }
            let operands = (for (v in stack) v)
            ops = ops ++ [{ op: r.value, operands: operands }]
            stack = []
            i = r.end
            continue
        }

        // otherwise a literal operand (number / name / string / hex / array)
        let r = read_operand(bytes, i)
        if (r.value != null) {
            stack = stack ++ [r.value]
        }
        i = r.end
    }

    return ops
}

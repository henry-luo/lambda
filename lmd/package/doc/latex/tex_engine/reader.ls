// Incremental TeX reader prototype. Category codes are consulted for each token.
fn ch(s, i) any^ => if (i >= len(s)) "" else slice(s, i, i + 1)

pub fn catcode(c, overrides) {
    let custom = overrides[c];
    if (custom != null) custom
    else if (c == "\\") 0
    else if (c == "%") 14
    else if (c == chr(0)) 9
    else if (c == chr(13)) 5
    else if (c == " ") 10
    else if (c == chr(127)) 15
    else if ((c >= "a" and c <= "z") or (c >= "A" and c <= "Z")) 11
    else 12
}

fn line_end(s, i) any^ =>
    if (i >= len(s) or ch(s, i) == "\n" or ch(s, i) == "\r") i else line_end(s, i + 1)
fn trim_end(s, start, end) any^ =>
    if (end > start and ch(s, end - 1) == " ") trim_end(s, start, end - 1) else end
fn after_line(s, end) any^ => if (ch(s, end) == "\r" and ch(s, end + 1) == "\n") end + 2
    else if (end < len(s)) end + 1 else end
fn utf8_width(c) => if (ord(c) < 128) 1 else if (ord(c) < 2048) 2 else if (ord(c) < 65536) 3 else 4
fn byte_count(s, i, end, total = 0) any^ => if (i >= end) total
    else byte_count(s, i + 1, end, total + utf8_width(ch(s, i)))

pub fn reader(source, file_id = 0) => {source: source, file_id: file_id, next_line: 0, line: "", position: 0,
    line_start: 0, line_end: 0, line_next: 0, loaded: false, eol_done: false,
    next_byte: 0, line_byte: 0, position_byte: 0,
    state: "new", endline: 13, catcodes: {}, origin: "local", end_input: false, seq: 0, line_no: 0}

pub fn load_line(r) any^ {
    if ((r.end_input and r.loaded) or r.next_line >= len(r.source)) null
    else {
        let end = line_end(r.source, r.next_line);
        let trimmed = trim_end(r.source, r.next_line, end);
        let next = after_line(r.source, end);
        {*:r, line: slice(r.source, r.next_line, trimmed), position: 0,
            line_start: r.next_line, line_end: trimmed, line_next: next,
            line_byte: r.next_byte, position_byte: 0,
            next_byte: r.next_byte + byte_count(r.source, r.next_line, next),
            next_line: next, loaded: true, eol_done: false, state: "new", line_no: r.line_no + 1}
    }
}

// raw verbatim delimiters are scanned before category-code tokenization.
fn verb_close(line, delimiter, position) any^ {
    if (position >= len(line)) null
    else if (ch(line, position)^ == delimiter) position
    else verb_close(line, delimiter, position + 1)
}

pub fn verb_line(r) any^ {
    let at = if (ch(r.line, r.position)^ == "*") r.position + 1 else r.position;
    let delimiter = ch(r.line, at)^;
    if (delimiter == "" or ord(delimiter) >= 128) {issue: "verb delimiter", reader: r}
    else {
        let close = verb_close(r.line, delimiter, at + 1)^;
        if (close == null) {issue: "verb unterminated", reader: r}
        else {
            let after = close + 1;
            let bytes = r.position_byte + byte_count(r.line, r.position, after)^;
            {issue: null, end: r.line_byte + bytes,
                reader: {*:r, position: after, position_byte: bytes, state: "mid", eol_done: false}}
        }
    }
}

fn raw_spaces(source, index) any^ =>
    if (ch(source, index)^ == " " or ch(source, index)^ == "\t") raw_spaces(source, index + 1) else index

fn raw_comment_end(source, index) any^ =>
    if (index >= len(source) or ch(source, index)^ == "\n" or ch(source, index)^ == "\r") index
    else raw_comment_end(source, index + 1)

fn raw_group_end(source, index, opener, closer, depth = 1) any^ {
    if (index >= len(source)) null
    else {
        let c = ch(source, index)^;
        if (c == "\\" and index + 1 < len(source)) raw_group_end(source, index + 2, opener, closer, depth)
        else if (c == "%") raw_group_end(source, raw_comment_end(source, index + 1)^, opener, closer, depth)
        else if (c == opener) raw_group_end(source, index + 1, opener, closer, depth + 1)
        else if (c == closer and depth == 1) index + 1
        else raw_group_end(source, index + 1, opener, closer, depth - if (c == closer) 1 else 0)
    }
}

fn raw_line_start(source, index) any^ =>
    if (index <= 0 or ch(source, index - 1)^ == "\n" or ch(source, index - 1)^ == "\r") index
    else raw_line_start(source, index - 1)

// raw capture can cross lines, so rebuild the reader at the absolute source position.
fn raw_seek(r, index) any^ {
    let start = raw_line_start(r.source, index)^;
    let end = line_end(r.source, index)^;
    let trimmed = trim_end(r.source, start, end)^;
    let next = after_line(r.source, end)^;
    let position = if (index < trimmed) index - start else trimmed - start;
    let line_byte = r.line_byte + byte_count(r.source, r.line_start, start)^;
    {*:r, line: slice(r.source, start, trimmed)^, position: position,
        line_start: start, line_end: trimmed, line_next: next, next_line: next,
        line_byte: line_byte, position_byte: byte_count(r.source, start, start + position)^,
        next_byte: line_byte + byte_count(r.source, start, next)^, loaded: true, eol_done: false, state: "mid"}
}

// raw document commands scan source characters before TeX tokenization.
pub fn raw_group_command(r) any^ {
    let source = r.source;
    let at = raw_spaces(source, r.line_start + r.position)^;
    let grouped = if (ch(source, at)^ == "{") raw_group_end(source, at + 1, "{", "}")^ else null;
    let end = if (ch(source, at)^ == "{") if (grouped == null) len(source) else grouped
        else at;
    {end: r.line_byte + byte_count(source, r.line_start, end)^, reader: raw_seek(r, end)^}
}

fn lower_hex(c) => c != "" and index_of("0123456789abcdef", c) != null
fn decode_super(line, at, c, after, cats) any^ {
    let next = ch(line, after);
    let digit = ch(line, after + 1);
    let digit2 = ch(line, after + 2);
    if (c == "" or catcode(c, cats) != 7 or ord(c) > 127 or next != c or digit == "")
        {value: c, end: after}
    else if (lower_hex(digit) and lower_hex(digit2))
        decode_super(line, at, chr(index_of("0123456789abcdef", digit) * 16 +
            index_of("0123456789abcdef", digit2)), after + 3, cats)
    else if (ord(digit) < 128)
        decode_super(line, at, chr(if (ord(digit) < 64) ord(digit) + 64 else ord(digit) - 64), after + 2, cats)
    else {value: c, end: after}
}

fn character(r, pos) any^ {
    let c = ch(r.line, pos);
    if (c == "") null else decode_super(r.line, pos, c, pos + 1, r.catcodes)
}

fn word(r, pos, parts) any^ {
    let c = character(r, pos);
    if (c == null or catcode(c.value, r.catcodes) != 11) {value: join(parts, ""), end: pos}
    else word(r, c.end, [*parts, c.value])
}

fn token(kind, value, cat, start, end) =>
    {kind: kind, value: value, cat: cat, start: start, end: end}
fn done(r, t) => if (t == null) {reader: r, token: null}
    else {reader: {*:r, seq: r.seq + 1}, token: {*:t, file: r.file_id, direct: true, seq: r.seq + 1}}

// Return one token so callers can change category codes between reads.
pub fn next(r) any^ {
    if (r == null) done(null, null)
    else if (not r.loaded or (r.position >= len(r.line) and r.eol_done)) next(load_line(r))
    else {
        let at_eol = r.position >= len(r.line);
        let c = if (at_eol) (if (r.endline >= 0 and r.endline <= 1114111)
            {value: chr(r.endline), end: r.position} else null) else character(r, r.position);
        let advanced = {*:r, position: if (c == null) r.position else c.end,
            position_byte: r.position_byte + if (c == null or at_eol) 0 else byte_count(r.line, r.position, c.end),
            eol_done: at_eol or r.eol_done};
        if (c == null) next(advanced)
        else {
            let cat = catcode(c.value, r.catcodes);
            let start = r.line_byte + r.position_byte;
            let end = if (at_eol) r.next_byte else r.line_byte + advanced.position_byte;
            if (cat == 0) control(advanced, start, at_eol)
            else if (cat == 13) done({*:advanced, state: "mid"}, token("active", c.value, cat, start, end))
            else if (cat == 5) {
                let consumed = {*:advanced, position: len(r.line), eol_done: true};
                if (r.state == "new") done(consumed, token("control", "par", null, start, r.next_byte))
                else if (r.state == "mid") done(consumed, token("char", " ", 10, start, r.next_byte))
                else next(consumed)
            }
            else if (cat == 9) next(advanced)
            else if (cat == 10) {
                if (r.state == "mid") done({*:advanced, state: "skip"}, token("char", " ", 10, start, end))
                else next(advanced)
            }
            else if (cat == 14) next({*:advanced, position: len(r.line), eol_done: true})
            else if (cat == 15) done(advanced, token("invalid", c.value, cat, start, end))
            else done({*:advanced, state: "mid"}, token("char", c.value, cat, start, end))
        }
    }
}

fn control(r, start, at_eol) any^ {
    let c = if (at_eol) null else character(r, r.position);
    if (c == null) done({*:r, eol_done: true, state: "mid"},
        token("control", if (r.endline >= 0 and r.endline <= 1114111) chr(r.endline) else "",
            null, start, r.next_byte))
    else if (catcode(c.value, r.catcodes) == 11) {
        let w = word(r, r.position, []);
        let end_byte = r.position_byte + byte_count(r.line, r.position, w.end);
        done({*:r, position: w.end, position_byte: end_byte, state: "skip"},
            token("control", w.value, null, start, r.line_byte + end_byte))
    }
    else {
        let end_byte = r.position_byte + byte_count(r.line, r.position, c.end);
        done({*:r, position: c.end, position_byte: end_byte, state: if (catcode(c.value, r.catcodes) == 10) "skip" else "mid"},
            token("control", c.value, null, start, r.line_byte + end_byte))
    }
}

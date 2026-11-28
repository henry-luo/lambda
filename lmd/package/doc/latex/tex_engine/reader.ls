// Incremental TeX scanning uses explicit snapshots so callers can change category codes.
type LineIndex = {length: int, parts: any}
type LineState = "new" | "mid" | "skip"
pub type Reader = {
    source: string, file_id: int, next_line: int, line: string, position: int,
    line_index: LineIndex, next_part: int,
    line_start: int, line_end: int, line_next: int, loaded: bool, eol_done: bool,
    next_byte: int, line_byte: int, position_byte: int, state: LineState,
    endline: int, catcodes: map, origin: string, end_input: bool, seq: int, line_no: int,
    preserve_source?: bool
}
type TokenKind = "char" | "control" | "active" | "invalid"
pub type Token = {
    kind: TokenKind, value: string, cat: int?, start: int, end: int,
    file?: int, direct?: bool, seq?: int
}
pub type ReadStep = {reader: Reader, token: Token}
pub type SourceSpan = {reader: Reader, end: int}
pub type RawEnvironmentSpan = {reader: Reader, end: int, closed: bool}
pub fn math_text_command(name) bool => contains(["text", "mbox", "hbox", "textrm", "textit", "textbf",
    "texttt", "textsf", "textsc", "emph"], name)
pub fn math_raw_group_command(name) bool => math_text_command(name) or name == "ce" or name == "pu"
let VERB_DELIMITER_ERROR = "verb delimiter"
let VERB_UNTERMINATED_ERROR = "verb unterminated"
let RAW_INLINE_DELIMITER_ERROR = "raw inline delimiter"
pub type CaptureError = error that ~.message in [VERB_DELIMITER_ERROR, VERB_UNTERMINATED_ERROR, RAW_INLINE_DELIMITER_ERROR]
type DelimiterScan = {end: int, present: bool, closed: bool}
type TextScan = {value: string, end: int}
type LineStep = {reader: Reader, eof: bool}
type LineBreak = \("\r\n" | "\r" | "\n")
fn text_scan(value: string, end: int) TextScan => {value:value, end:end}
fn delimiter_scan(end: int, closed: bool, present: bool = true) DelimiterScan =>
    {end:end, present:present, closed:closed}

pub fn definition_command(name: string) bool => contains(["def", "gdef", "edef", "xdef", "newcommand", "renewcommand",
    "providecommand", "newenvironment", "renewenvironment"], name)

// The span normalizes an empty upper slice, while negative indices must not wrap.
fn ch(s: string, i: int) string => if (i < 0) "" else text_span(s, i, i + 1)

// Query absence is normalized once, at the source-reading boundary (S7.10.7).
pub fn text_span(s: string, start: int, end: int) string => slice(s, start, end) or ""
fn code_point(c: string) int => ord(c) or 0

pub fn catcode(c: string, overrides: map) int {
    let custom = overrides[c];
    // A valid override supplies the category without computing the default.
    if (custom is int) {
        let code = int(custom) ^ { -1 };
        if (code >= 0 and code <= 15) { return code }
    }
    return if (c == "\\") 0
        else if (c == "%") 14
        else if (c == "\u{0}") 9
        else if (c == "\r") 5
        else if (c == " ") 10
        else if (c == "\u{7f}") 15
        else if ((c >= "a" and c <= "z") or (c >= "A" and c <= "Z")) 11 else 12
}

fn line_end(s: string, i: int) int {
    var at = i
    while (at < len(s) and ch(s, at) != "\n" and ch(s, at) != "\r") { at = at + 1 }
    return at
}

fn trim_end(s: string, end: int) int {
    var at = end
    while (at > 0 and ch(s, at - 1) == " ") { at = at - 1 }
    return at
}
// Source positions count the original UTF-8 bytes, including any superscript spelling.
fn byte_count(s: string, i: int, end: int) int => len(binary(text_span(s, i, end)) ^ { null })

fn character_bytes(line: string, start: int, scan: TextScan) int {
    if (scan.end != start + 1) byte_count(line, start, scan.end)
    else {
        let code = code_point(scan.value);
        if (code < 128) 1 else if (code < 2048) 2 else if (code < 65536) 3 else 4
    }
}

pub fn reader(source: string, file_id: int = 0) Reader => {source: source, file_id: file_id, next_line: 0, line: "", position: 0,
    line_index: {length:len(source), parts:split(source, LineBreak, true) ^ { [] }}, next_part: 0,
    line_start: 0, line_end: 0, line_next: 0, loaded: false, eol_done: false,
    next_byte: 0, line_byte: 0, position_byte: 0,
    state: "new", endline: 13, catcodes: {}, origin: "local", end_input: false, seq: 0, line_no: 0}

// Splitting preserves each terminator, so advancing a line only measures its own text.
fn line_part(index: LineIndex, part: int) string? => match (index.parts[part] ^ { null }) {
    case string: ~ default: null
}
fn physical_line(r: Reader) Reader {
    let raw = (line_part(r.line_index, r.next_part) ^ { null }) or "";
    let ending = len(line_part(r.line_index, r.next_part + 1) ^ { null });
    let size = len(raw);
    let trimmed = trim_end(raw, size);
    let next = r.next_line + size + ending;
    {*:r, line: if (trimmed == size) raw else text_span(raw, 0, trimmed), position: 0,
        line_start: r.next_line, line_end: r.next_line + trimmed, line_next: next,
        line_byte: r.next_byte, position_byte: 0, next_byte: r.next_byte + len(binary(raw) ^ { null }) + ending,
        next_line: next, next_part: r.next_part + 2, loaded: true, eol_done: false,
        state: "new", line_no: r.line_no + 1}
}

fn read_line(r: Reader) LineStep => if ((r.end_input and r.loaded) or r.next_line >= r.line_index.length or
        r.next_part >= (len(r.line_index.parts) ^ { 0 }))
    {reader:r, eof:true} else {reader:physical_line(r), eof:false}

pub fn load_line(r: Reader) Reader? {
    let step = read_line(r);
    if (step.eof) null else step.reader
}

// raw verbatim delimiters are scanned before category-code tokenization.
fn verb_close(line: string, delimiter: string, position: int) DelimiterScan {
    var at = position
    while (at < len(line)) {
        if (ch(line, at) == delimiter) { return delimiter_scan(at, true) }
        at = at + 1
    }
    return delimiter_scan(at, false)
}

pub fn verb_line(r: Reader) SourceSpan | CaptureError {
    let at = if (ch(r.line, r.position) == "*") r.position + 1 else r.position;
    let delimiter = ch(r.line, at);
    if (delimiter == "" or code_point(delimiter) >= 128) raise error(VERB_DELIMITER_ERROR)
    else {
        let close = verb_close(r.line, delimiter, at + 1);
        if (not close.closed) raise error(VERB_UNTERMINATED_ERROR)
        else {
            let after = close.end + 1;
            let bytes = r.position_byte + byte_count(r.line, r.position, after);
            {end: r.line_byte + bytes,
                reader: {*:r, position: after, position_byte: bytes, state: "mid", eol_done: false}}
        }
    }
}

fn raw_spaces(source: string, index: int) int {
    var at = index
    while (ch(source, at) == " " or ch(source, at) == "\t") { at = at + 1 }
    return at
}

pub fn raw_group_end(source: string, index: int, opener: string, closer: string) DelimiterScan {
    var at = index, depth = 1
    while (at < len(source)) {
        let c = ch(source, at);
        if (c == "\\" and at + 1 < len(source)) { at = at + 2 }
        else if (c == "%") { at = line_end(source, at + 1) }
        else {
            if (c == opener) { depth = depth + 1 }
            else if (c == closer) {
                depth = depth - 1;
                if (depth == 0) { return delimiter_scan(at + 1, true) }
            }
            at = at + 1
        }
    }
    return delimiter_scan(at, false)
}

// raw capture can cross lines, so rebuild the reader at the absolute source position.
fn raw_seek(r: Reader, index: int) Reader {
    var current: Reader = if (r.loaded) r else physical_line(r);
    while (index >= current.line_next and (line_part(current.line_index, current.next_part - 1) ^ { null }) != null) {
        current = physical_line(current)
    }
    let position = (if (index < current.line_end) index else current.line_end) - current.line_start;
    {*:current, position:position, position_byte:byte_count(current.line, 0, position),
        eol_done:false, state:"mid", line_no:r.line_no}
}

// native TeX preserves these environment bodies as source rather than executing them.
pub fn raw_environment_name(name: string) bool => contains(["tikzpicture", "verbatim", "lstlisting", "minted",
    "alltt", "Verbatim", "BVerbatim", "LVerbatim", "SaveVerbatim", "VerbatimOut", "fancyvrb", "comment",
    "filecontents", "filecontents*", "luacode", "luacode*"], name)

// scan for the exact source terminator without tokenizing the environment body.
pub fn raw_environment(r: Reader, name: string) RawEnvironmentSpan {
    let source = r.source;
    let closing = "\\end{" ++ name ++ "}";
    var at = r.line_start + r.position;
    while (at < len(source) and text_span(source,at,at + len(closing)) != closing) { at = at + 1 }
    let found = at < len(source);
    let end = if (found) at + len(closing) else len(source);
    {end:r.line_byte + byte_count(source,r.line_start,end),reader:raw_seek(r,end),closed:found}
}

// raw document commands scan source characters before TeX tokenization.
pub fn raw_group_command(r: Reader) SourceSpan {
    let source = r.source;
    let at = raw_spaces(source, r.line_start + r.position);
    let grouped = if (ch(source, at) == "{") raw_group_end(source, at + 1, "{", "}") else delimiter_scan(at,false,false);
    let end = if (ch(source, at) == "{") (if (grouped.closed) grouped.end else len(source))
        else at;
    {end: r.line_byte + byte_count(source, r.line_start, end), reader: raw_seek(r, end)}
}

fn raw_inline_close(source: string, index: int, delimiter: string) DelimiterScan {
    var at = index
    while (at < len(source) and ch(source, at) != "\n") {
        if (ch(source, at) == delimiter) { return delimiter_scan(at, true) }
        at = at + 1
    }
    return delimiter_scan(at, false)
}

// listings and minted capture their options and code before tokenization.
pub fn raw_inline_command(r: Reader, language: bool) SourceSpan | CaptureError {
    let source = r.source;
    let start = raw_spaces(source,r.line_start + r.position);
    let starred = if (ch(source,start) == "*") start + 1 else start;
    let option_end = if (ch(source,starred) == "[") raw_group_end(source,starred + 1,"[","]") else delimiter_scan(starred,false,false);
    // Preserve the existing fallback when an optional group is not closed.
    let after_option = if (option_end.present and option_end.closed) option_end.end else starred;
    let language_end = if (language and ch(source,after_option) == "{")
        raw_group_end(source,after_option + 1,"{","}") else delimiter_scan(after_option,false,false);
    let at = if (language_end.present and language_end.closed) language_end.end else after_option;
    let delimiter = ch(source,at);
    let grouped = if (delimiter == "{") raw_group_end(source,at + 1,"{","}") else delimiter_scan(at,false,false);
    if (delimiter != "" and delimiter != "{" and code_point(delimiter) >= 128)
        raise error(RAW_INLINE_DELIMITER_ERROR)
    else {
        let end = if (delimiter == "{") (if (grouped.closed) grouped.end else len(source))
            else if (delimiter == "") at else {
                let close = raw_inline_close(source,at + 1,delimiter);
                // A missing closing delimiter recovers at the line boundary.
                if (close.closed) close.end + 1 else close.end
            };
        {end:r.line_byte + byte_count(source,r.line_start,end),reader:raw_seek(r,end)}
    }
}

fn hex_digit(c: string) int => if (c == "") -1 else index_of("0123456789abcdef", c) or -1

fn decode_super(line: string, initial: string, after: int, cats: map) TextScan {
    var value = initial, end = after
    while (value != "") {
        // Category 7 is supplied by an override, so default categories need no lookup.
        let code = cats[value];
        if (not (code is int) or code != 7 or code_point(value) > 127 or ch(line, end) != value) { break }
        let digit = ch(line, end + 1);
        let high = hex_digit(digit), low = hex_digit(ch(line, end + 2));
        if (digit == "") { break }
        if (high >= 0 and low >= 0) {
            value = chr(high * 16 + low);
            end = end + 3
        }
        else if (code_point(digit) < 128) {
            let code = code_point(digit);
            value = chr(if (code < 64) code + 64 else code - 64);
            end = end + 2
        }
        else { break }
    }
    return text_scan(value, end)
}

fn character(r: Reader, pos: int) TextScan {
    let c = ch(r.line, pos);
    // Echoed definitions retain the source spelling before TeX's ^^ decoding.
    if (c == "" or r.preserve_source == true) { return text_scan(c, pos + if (c == "") 0 else 1) }
    return decode_super(r.line, c, pos + 1, r.catcodes)
}

fn word(r: Reader, pos: int, initial: string) TextScan {
    var at = pos, start = pos, text = initial
    while (true) {
        let raw = ch(r.line, at);
        if (raw == "") { break }
        let cat = catcode(raw, r.catcodes);
        if (cat == 11) { at = at + 1 }
        // Only a superscript pair can turn a nonletter into part of a control word.
        else if (cat == 7 and r.preserve_source != true) {
            let c = decode_super(r.line, raw, at + 1, r.catcodes);
            if (catcode(c.value, r.catcodes) != 11) { break }
            text = text ++ text_span(r.line, start, at) ++ c.value;
            start = c.end;
            at = c.end
        }
        else { break }
    }
    return text_scan(text ++ text_span(r.line, start, at), at)
}

fn token(r: Reader, kind: TokenKind, value: string, cat: int?, start: int, end: int) Token =>
    {kind:kind, value:value, cat:cat, start:start, end:end, file:r.file_id, direct:true, seq:r.seq + 1}
// Publish the final cursor and sequence together for a returned token.
fn done(r: Reader, t: Token, position: int, position_byte: int, eol_done: bool, line_state: LineState | "") ReadStep =>
    {reader: {*:r, position:position, position_byte:position_byte, eol_done:eol_done,
        state:if (line_state == "") r.state else line_state, seq:r.seq + 1},
        token: t}

fn advance(r: Reader, position: int, position_byte: int, eol_done: bool) Reader =>
    {*:r, position:position, position_byte:position_byte, eol_done:eol_done}

// Return one token so callers can change category codes between reads.
pub fn next(r: Reader?) ReadStep? {
    // Ignored input can span arbitrarily many physical lines.
    var current: Reader = r or reader("")
    while (true) {
        let at_eol = current.position >= len(current.line);
        if ((not current.loaded or (at_eol and current.eol_done))) {
            let step = read_line(current);
            if (step.eof) { return null }
            current = step.reader
        }
        else {
            let c = if (at_eol) (if (current.endline >= 0 and current.endline <= 1114111)
                text_scan(chr(current.endline), current.position) else text_scan("", current.position)) else character(current, current.position);
            let position = if (c.value == "") current.position else c.end;
            let position_byte = current.position_byte + if (c.value == "" or at_eol) 0
                else character_bytes(current.line, current.position, c);
            let eol_done = at_eol or current.eol_done;
            if (c.value == "") { current = advance(current, position, position_byte, eol_done) }
            else {
                let cat = catcode(c.value, current.catcodes);
                let start = current.line_byte + current.position_byte;
                let end = if (at_eol) current.next_byte else current.line_byte + position_byte;
                if (cat == 0) { return control(advance(current, position, position_byte, eol_done), start, at_eol) }
                else if (cat == 13) {
                    return done(current, token(current, "active", c.value, cat, start, end), position, position_byte, eol_done, "mid")
                }
                else if (cat == 5) {
                    let line_end = len(current.line);
                    if (current.state != "skip") {
                        let ending = if (current.state == "new") token(current, "control", "par", null, start, current.next_byte)
                            else token(current, "char", " ", 10, start, current.next_byte);
                        return done(current, ending, line_end, position_byte, true, "")
                    }
                    current = advance(current, line_end, position_byte, true)
                }
                else if (cat == 9) { current = advance(current, position, position_byte, eol_done) }
                else if (cat == 10) {
                    if (current.state == "mid") {
                        return done(current, token(current, "char", " ", 10, start, end), position, position_byte, eol_done, "skip")
                    }
                    current = advance(current, position, position_byte, eol_done)
                }
                else if (cat == 14) { current = advance(current, len(current.line), position_byte, true) }
                else if (cat == 15) {
                    return done(current, token(current, "invalid", c.value, cat, start, end), position, position_byte, eol_done, "")
                }
                else { return done(current, token(current, "char", c.value, cat, start, end), position, position_byte, eol_done, "mid") }
            }
        }
    }
    return null
}

fn control(r: Reader, start: int, at_eol: bool) ReadStep {
    let c = if (at_eol) text_scan("", r.position) else character(r, r.position);
    let cat = catcode(c.value, r.catcodes);
    if (c.value == "") done(r, token(r, "control", if (r.endline >= 0 and r.endline <= 1114111) chr(r.endline) else "",
        null, start, r.next_byte), r.position, r.position_byte, true, "mid")
    else if (cat == 11) {
        // The escape scanner already consumed and classified the first letter.
        let w = word(r, c.end, c.value);
        let end_byte = r.position_byte + byte_count(r.line, r.position, w.end);
        done(r, token(r, "control", w.value, null, start, r.line_byte + end_byte), w.end, end_byte, r.eol_done, "skip")
    }
    else {
        let end_byte = r.position_byte + character_bytes(r.line, r.position, c);
        done(r, token(r, "control", c.value, null, start, r.line_byte + end_byte), c.end, end_byte, r.eol_done,
            if (cat == 10) "skip" else "mid")
    }
}

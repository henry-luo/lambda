import search: lambda.io.search

// Byte cursors keep source offsets exact, including on long UTF-8 lines.
pub type Source = {bytes: binary?, length: int, lines: int[], continuation: int[]}

pub fn source(bytes: binary?) Source => {
    bytes: bytes,
    length: len(bytes),
    lines: [0, *[for (i,b in bytes where b == 10) i + 1]],
    continuation: [for (i,b in bytes where b >= 128 and b < 192) i]
}

pub fn byte(s: Source, pos: int) int^ => if (pos < 0 or pos >= s.length) -1 else (s.bytes[pos] or -1)

pub fn text(s: Source, first: int, stop: int) string^ =>
    if (first >= stop) "" else lambda.sys.parse(slice(s.bytes, first, stop), 'text')^

pub fn starts(s: Source, pos: int, literal: binary) bool^ =>
    pos + len(literal) <= s.length and slice(s.bytes, pos, pos + len(literal)) == literal

pub fn ascii_alpha(b: int) bool => (b >= 65 and b <= 90) or (b >= 97 and b <= 122)
pub fn digit(b: int) bool => b >= 48 and b <= 57
pub fn space(b: int) bool => b == 32 or b == 9 or b == 10 or b == 13

pub fn line_end(s: Source, start: int) int^ {
    var pos = start
    while (pos < s.length and byte(s, pos)^ != 10) { pos = pos + 1 }
    return pos
}

pub fn trivia(s: Source, start: int, line_comments: binary[], block_comments: bool = false,
              newlines: bool = true) int^ {
    var pos = start
    while (pos < s.length) {
        let b = byte(s, pos)^
        if (space(b) and (newlines or (b != 10 and b != 13))) { pos = pos + 1 }
        else if (block_comments and b == 47 and byte(s, pos + 1)^ == 42) {
            pos = pos + 2
            while (pos < s.length and (byte(s, pos)^ != 42 or byte(s, pos + 1)^ != 47)) {
                pos = pos + 1
            }
            if (pos < s.length) { pos = pos + 2 }
        } else if (any([for (marker in line_comments) starts(s, pos, marker)^])) {
            pos = line_end(s, pos)^
        } else { break }
    }
    return pos
}

pub fn location(s: Source, pos: int) map^ {
    let line = search.lower_bound(s.lines, pos + 1)^
    let first = s.lines[line - 1]
    let extra = search.lower_bound(s.continuation, pos)^ - search.lower_bound(s.continuation, first)^
    return {line: line, column: pos - first + 1 - extra}
}

pub fn span(s: Source, first: int, stop: int) map^ {
    let loc = location(s, first)^;
    {'source-start': first, 'source-end': stop, 'source-line': loc.line, 'source-column': loc.column}
}

// Reverse links allow pure parsers to accumulate an unknown number of items in linear work.
pub fn add(items, value) => {value: value, previous: items, count: (items.count or 0) + 1}

pub fn finish(items) array^ {
    let count = items.count or 0
    if (count == 0) { return [] }
    var result: any[] = [for (i in 1 to count) null]
    var link = items
    var pos = count
    while (pos > 0) {
        pos = pos - 1
        result[pos] = link.value
        link = link.previous
    }
    return result
}

pub fn problem(offset: int, message: string) => {offset: offset, message: message}

pub fn quoted(s: Source, start: int, escape) map^ {
    var pieces = null
    var first = start + 1
    var pos = first
    while (pos < s.length) {
        let current = byte(s, pos)^
        if (current == 34) {
            let tail = text(s, first, pos)^
            let value = if (pieces == null) tail else join([*finish(pieces)^, tail], "")
            return {value: value, next: pos + 1, issue: null}
        }
        if (current == 92) {
            if (pos + 1 == s.length) {
                return {value: null, next: pos + 1, issue: problem(pos + 1, "Unterminated string escape")}
            }
            let decoded = escape(s, pos + 1)^
            if (pos > first) { pieces = add(pieces, text(s, first, pos)^) }
            pieces = add(pieces, decoded.value)
            pos = decoded.next
            first = pos
        } else { pos = pos + 1 }
    }
    return {value: null, next: pos, issue: problem(start, "Unterminated quoted string")}
}

pub fn context_line(s: Source, line: int) string^ {
    let first = s.lines[line - 1]
    var stop = (s.lines[line] or (s.length + 1)) - 1
    while (stop > first and byte(s, stop - 1)^ == 13) { stop = stop - 1 }
    return text(s, first, stop)^
}

pub fn diagnostic(s: Source, pos: int, code: string, message: string, severity: string = "error") element^ {
    let loc = location(s, pos)^
    let context = context_line(s, loc.line)^;
    <diagnostic code:code, severity:severity, message:message,
        'source-start':pos, 'source-line':loc.line, 'source-column':loc.column,
        *:if (context == "") {} else {context:context}>
}

fn hex4(s, start: int) int^ {
    var n = 0
    var pos = start
    while (pos < start + 4) {
        let b = byte(s, pos)^
        let digit = if (digit(b)) b - 48
            else if (b >= 65 and b <= 70) b - 55 else if (b >= 97 and b <= 102) b - 87 else -1
        if (digit < 0) { return -1 }
        n = n * 16 + digit;
        pos = pos + 1
    }
    return n
}

fn unicode_escape(s, pos: int) map^ {
    let hi = hex4(s, pos + 1)^
    if (hi < 0) {value: "�", next: pos + 1}
    else if (hi >= 55296 and hi <= 56319) {
        let lo = if (byte(s, pos + 5)^ == 92 and byte(s, pos + 6)^ == 117)
            hex4(s, pos + 7)^ else -1
        if (lo >= 56320 and lo <= 57343)
            {value: chr(65536 + (hi - 55296) * 1024 + lo - 56320), next: pos + 11}
        else {value: "�", next: pos + 5}
    } else {value: if (hi >= 56320 and hi <= 57343) "�" else chr(hi), next: pos + 5}
}

let escapes = {b: "\b", f: "\f", n: "\n", r: "\r", t: "\t"}

pub fn standard_escape(s: Source, pos: int) map^ {
    let b = byte(s, pos)^
    if (b == 117) unicode_escape(s, pos)^
    else {
        let plain = text(s, pos, pos + 1)^;
        {value: escapes[plain] or plain, next: pos + 1}
    }
}

// The native Mark attribute API stopped at a decoded NUL.
pub fn c_string(value: string) string {
    let nul = index_of(value, chr(0))
    return if (nul == null) value else slice(value, 0, nul) or ""
}

pub fn inline_space(s: Source, start: int, cr: bool = true) int^ {
    var pos = start
    while (true) {
        let b = byte(s, pos)^
        if (b == 32 or b == 9 or (cr and b == 13)) { pos = pos + 1 } else { break }
    }
    return pos
}

pub fn set_cursor(st: map, pos: int) map => {*:st, pos:pos}

pub fn issue(s, st: map, pos: int, code: string, message: string, severity: string = "error") map^ =>
    if ((st.errors.count or 0) >= 100) st
    else {*:st, errors:add(st.errors, diagnostic(s, pos, code, message, severity)^)}


pub fn brace_end(s: Source, start: int) int^ {
    var pos = start
    var depth = 1
    while (pos < s.length and depth > 0) {
        let b = byte(s, pos)^
        if (b == 123) { depth = depth + 1 }
        else if (b == 125) { depth = depth - 1 }
        pos = pos + 1
    }
    return pos
}

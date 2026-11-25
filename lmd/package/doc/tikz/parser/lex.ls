import scan: lambda.io.source_scan

pub fn space(b: int) bool => scan.space(b) or b in [11,12]

fn newline(s, start: int, limit: int) int^ {
    var pos = start
    while (pos < limit and not (scan.byte(s, pos)^ in [10,13])) { pos = pos + 1 }
    return pos
}

pub fn skip(s, start: int, limit: int, comments: bool = true) int^ {
    var pos = start
    while (pos < limit) {
        let b = scan.byte(s, pos)^
        if (space(b)) { pos = pos + 1 }
        else if (comments and b == 37) { pos = newline(s, pos + 1, limit)^ }
        else { break }
    }
    return pos
}

pub fn trimmed(s, start: int, stop: int, comments: bool = false) map^ {
    let first = skip(s, start, stop, comments)^
    var end_pos = stop
    while (end_pos > first and space(scan.byte(s, end_pos - 1)^)) { end_pos = end_pos - 1 }
    return {first:first, stop:end_pos}
}

pub fn word(s, start: int, limit: int, expected: string, bounded: bool = true) int?^ {
    let pos = skip(s, start, limit)^
    let needle = binary(expected)^
    let stop = pos + len(needle)
    return if (stop <= limit and scan.starts(s, pos, needle)^ and
        (not bounded or stop == limit or not scan.ascii_alpha(scan.byte(s, stop)^))) stop else null
}

fn command_byte(b: int) bool => scan.ascii_alpha(b) or b == 64

pub fn command(s, start: int, limit: int) map?^ {
    if (start >= limit or scan.byte(s, start)^ != 92) { return null }
    let first = start + 1
    var stop = first
    if (stop < limit and command_byte(scan.byte(s, stop)^)) {
        while (stop < limit and command_byte(scan.byte(s, stop)^)) { stop = stop + 1 }
    } else if (stop < limit) { stop = stop + 1 }
    return {name:scan.text(s, first, min(first + 95, stop))^, next:stop}
}

pub fn balanced(s, start: int, limit: int, opening: int, closing: int) map?^ {
    if (start >= limit or scan.byte(s, start)^ != opening) { return null }
    var pos = start + 1
    var depth = 1
    while (pos < limit) {
        let b = scan.byte(s, pos)^
        if (b == 92 and pos + 1 < limit) { pos = pos + 2 }
        else if (b == 37) { pos = newline(s, pos + 1, limit)^ }
        else {
            if (b == opening) { depth = depth + 1 }
            else if (b == closing) {
                depth = depth - 1
                if (depth == 0) { return {first:start + 1, stop:pos, next:pos + 1} }
            }
            pos = pos + 1
        }
    }
    return null
}

pub fn located(st, pos: int) int => st.anchor or pos

pub fn fail(st, pos: int, message: string) map =>
    {*:st, pos:pos, errors:[*st.errors, scan.problem(located(st, pos), "tikz: " ++ message)]}

pub fn expected_group(s, st, opening: int, closing: int) map^ {
    let pos = skip(s, st.pos, s.length)^
    let span = balanced(s, pos, s.length, opening, closing)^
    if (span == null) {
        return {cursor:fail(st, pos, if (opening == 123) "expected balanced group" else "expected balanced options"),
            value:null}
    }
    return {cursor:scan.set_cursor(st, span.next), value:span}
}

pub fn value(s, span) string^ => scan.text(s, span.first, span.stop)^

pub fn literal_name(s, first: int, stop: int) bool^ {
    if (first >= stop) { return false }
    let initial = scan.byte(s, first)^
    if (not (scan.ascii_alpha(initial) or initial == 95)) { return false }
    var pos = first + 1
    while (pos < stop) {
        let b = scan.byte(s, pos)^
        if (not (scan.ascii_alpha(b) or scan.digit(b) or b in [45,95])) { return false }
        pos = pos + 1
    }
    return true
}

// PGF uses mixed nested delimiters in option keys and values.
pub fn separator(s, start: int, stop: int, delim: int, brackets: bool = true,
                 comments: bool = false, escapes: bool = false) int^ {
    var pos = start
    var depth = 0
    while (pos < stop) {
        let b = scan.byte(s, pos)^
        if (escapes and b == 92 and pos + 1 < stop) { pos = pos + 2 }
        else if (comments and b == 37) { pos = newline(s, pos + 1, stop)^ }
        else {
            if (b == delim and depth == 0) { break }
            if (b in [123,40] or (brackets and b == 91)) { depth = depth + 1 }
            else if (depth > 0 and (b in [125,41] or (brackets and b == 93))) { depth = depth - 1 }
            pos = pos + 1
        }
    }
    return pos
}

pub fn statement_end(s, start: int, limit: int) int?^ {
    var pos = start
    var braces = 0
    var parens = 0
    var brackets = 0
    while (pos < limit) {
        let b = scan.byte(s, pos)^
        if (b == 92 and pos + 1 < limit) { pos = pos + 2 }
        else if (b == 37) { pos = min(newline(s, pos + 1, limit)^ + 1, limit) }
        else {
            if (b == 59 and braces == 0 and parens == 0 and brackets == 0) { return pos }
            if (b == 123) { braces = braces + 1 }
            else if (b == 125 and braces > 0) { braces = braces - 1 }
            else if (b == 40) { parens = parens + 1 }
            else if (b == 41 and parens > 0) { parens = parens - 1 }
            else if (b == 91) { brackets = brackets + 1 }
            else if (b == 93 and brackets > 0) { brackets = brackets - 1 }
            pos = pos + 1
        }
    }
    return null
}

// Lexical C/C++ function discovery shared by native GC source checks.
fn c_scan_ident(ch: string) bool =>
    len(ch) == 1 and contains("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_", ch)
fn c_scan_start(ch: string) bool =>
    len(ch) == 1 and contains("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_", ch)
fn c_scan_space(ch: string) bool =>
    len(ch) == 1 and contains(" \t\r\n", ch)

pn c_find(source: string, pattern: string, from: int) {
    let tail = slice(source, from)
    let byte_offset = index_of(tail, pattern)
    if (byte_offset == null) { return null }
    var codepoints = 0
    var bytes = 0
    while (bytes < byte_offset) {
        let code = ord(slice(tail, codepoints, codepoints + 1))
        bytes = bytes + if (code < 128) 1 else if (code < 2048) 2
                else if (code < 65536) 3 else 4
        codepoints = codepoints + 1
    }
    return from + codepoints
}

pn c_spaces(width: int) {
    var out = ""
    var block = " "
    var remaining = width
    while (remaining > 0) {
        let size = min(remaining, len(block))
        out = out ++ slice(block, 0, size)
        remaining = remaining - size
        if (remaining > 0) { block = block ++ block }
    }
    return out
}

pn c_quote_end(line: string, from: int, quote: string) {
    var cursor = from
    while (cursor < len(line)) {
        let ch = slice(line, cursor, cursor + 1)
        if (ch == "\\" and cursor + 1 < len(line)) { cursor = cursor + 2 }
        else {
            cursor = cursor + 1
            if (ch == quote) { return {end: cursor, closed: true} }
        }
    }
    return {end: cursor, closed: false}
}

pub pn c_clean_source(source: string) {
    var chunks = []
    var batch = []
    var block_comment = false
    var quote = ""
    for (line in split(source, "\n")) {
        var pieces = []
        var pos = 0
        while (pos < len(line)) {
            if (block_comment) {
                let close = c_find(line, "*/", pos)
                let end = if (close == null) len(line) else close + 2
                push(pieces, c_spaces(end - pos))
                pos = end
                if (close != null) { block_comment = false }
            } else if (quote != "") {
                let span = c_quote_end(line, pos, quote)
                push(pieces, c_spaces(span.end - pos))
                pos = span.end
                if (span.closed) { quote = "" }
            } else {
                var hit = null
                var marker = ""
                for (candidate in ["//", "/*", "\"", "'"]) {
                    let found = c_find(line, candidate, pos)
                    if (found != null and (hit == null or found < hit)) {
                        hit = found
                        marker = candidate
                    }
                }
                if (hit == null) { push(pieces, slice(line, pos)); pos = len(line) }
                else {
                    if (hit > pos) { push(pieces, slice(line, pos, hit)) }
                    if (marker == "//") {
                        push(pieces, c_spaces(len(line) - hit))
                        pos = len(line)
                    } else if (marker == "/*") {
                        push(pieces, "  ")
                        pos = hit + 2
                        block_comment = true
                    } else {
                        quote = marker
                        let span = c_quote_end(line, hit + 1, quote)
                        push(pieces, c_spaces(span.end - hit))
                        pos = span.end
                        if (span.closed) { quote = "" }
                    }
                }
            }
        }
        push(batch, join(pieces, ""))
        if (len(batch) == 128) {
            push(chunks, join(batch, "\n"))
            batch = []
        }
    }
    if (len(batch) > 0) { push(chunks, join(batch, "\n")) }
    return join(chunks, "\n")
}

pn c_scan_pairs(clean: string) {
    var braces = []
    var parens = []
    // Store sparse pairs compactly; wide indexed writes copy too much data.
    var brace_pairs = []
    var paren_pairs = []
    var offset = 0
    for (line in split(clean, "\n")) {
        var cursor = 0
        while (cursor < len(line)) {
            var next_pos = null
            var ch = ""
            for (token in ["{", "}", "(", ")"]) {
                let found = c_find(line, token, cursor)
                if (found != null and (next_pos == null or found < next_pos)) {
                    next_pos = found
                    ch = token
                }
            }
            if (next_pos == null) { break }
            cursor = next_pos
            let pos = offset + cursor
            if (ch == "{") { push(braces, pos) }
            else if (ch == "}" and len(braces) > 0) {
                let start_pos = braces[len(braces) - 1]
                braces = slice(braces, 0, len(braces) - 1)
                push(brace_pairs, {key: start_pos, value: pos})
            } else if (ch == "(") { push(parens, pos) }
            else if (ch == ")" and len(parens) > 0) {
                let start_pos = parens[len(parens) - 1]
                parens = slice(parens, 0, len(parens) - 1)
                push(paren_pairs, {key: pos, value: start_pos})
            }
            cursor = cursor + 1
        }
        offset = offset + len(line) + 1
    }
    return {braces: sort(brace_pairs, ~.key), parens: paren_pairs}
}

pn c_pair_lookup(pairs, key: int) {
    var low = 0
    var high = len(pairs)
    while (low < high) {
        let middle = (low + high) div 2
        if (pairs[middle].key < key) { low = middle + 1 }
        else { high = middle }
    }
    if (low < len(pairs) and pairs[low].key == key) { return pairs[low].value }
    return -1
}

pn c_scan_name(clean: string, open_paren: int) {
    let lower = max(0, open_paren - 180)
    var start = open_paren
    while (start > lower) {
        let ch = slice(clean, start - 1, start)
        if (c_scan_ident(ch) or ch == ":" or ch == "~") { start = start - 1 }
        else { break }
    }
    if (start == open_paren) { return null }
    let qualified = slice(clean, start, open_paren)
    let parts = split(qualified, "::")
    if (join(parts, "::") != qualified) { return null }
    for (part in parts) {
        if (len(part) == 0) { return null }
        let first = slice(part, 0, 1)
        let initial = if (first == "~") slice(part, 1, 2) else first
        if (not c_scan_start(initial)) { return null }
        var pos = if (first == "~") 2 else 1
        while (pos < len(part)) {
            if (not c_scan_ident(slice(part, pos, pos + 1))) { return null }
            pos = pos + 1
        }
    }
    return {start: start, name: parts[len(parts) - 1]}
}

pn c_scan_prefix_rejected(prefix: string) {
    var pos = 0
    while (pos < len(prefix)) {
        let ch = slice(prefix, pos, pos + 1)
        if (ch == "=") {
            let before = if (pos == 0) "" else slice(prefix, pos - 1, pos)
            let after = slice(prefix, pos + 1, pos + 2)
            if (not contains(["=", "!", "<", ">"], before) and after != "=") {
                return true
            }
        }
        if (c_scan_start(ch)) {
            let start = pos
            while (pos < len(prefix) and c_scan_ident(slice(prefix, pos, pos + 1))) {
                pos = pos + 1
            }
            if (contains(["return", "case", "new"], slice(prefix, start, pos))) {
                return true
            }
        } else { pos = pos + 1 }
    }
    return false
}

fn c_scan_non_call(name: string) bool => contains([
    "alignas", "alignof", "asm", "catch", "decltype", "defined", "do", "for", "if",
    "no_gc", "return", "sizeof", "static_assert", "switch", "while"
], name)

pn c_line_index(starts, position: int) {
    var low = 0
    var high = len(starts)
    while (low + 1 < high) {
        let middle = (low + high) div 2
        if (starts[middle] <= position) { low = middle }
        else { high = middle }
    }
    return low
}

pn c_segment(lines, starts, from: int, to: int) {
    if (to <= from) { return "" }
    let first = c_line_index(starts, from)
    let final_line = c_line_index(starts, to)
    if (first == final_line) {
        return slice(lines[first], from - starts[first], to - starts[first])
    }
    var sections = [slice(lines[first], from - starts[first])]
    var line_index = first + 1
    while (line_index < final_line) {
        push(sections, lines[line_index])
        line_index = line_index + 1
    }
    push(sections, slice(lines[final_line], 0, to - starts[final_line]))
    return join(sections, "\n")
}

pn c_previous_boundary(lines, starts, position: int) {
    var line_index = c_line_index(starts, position)
    var cursor = position - starts[line_index]
    while (line_index >= 0) {
        let line = lines[line_index]
        while (cursor > 0) {
            cursor = cursor - 1
            let ch = slice(line, cursor, cursor + 1)
            if (ch == ";" or ch == "}" or ch == "{") {
                return starts[line_index] + cursor
            }
        }
        line_index = line_index - 1
        if (line_index >= 0) { cursor = len(lines[line_index]) }
    }
    return -1
}

pub pn c_function_bodies(path: string, source: string) {
    let clean = c_clean_source(source)
    let pairs = c_scan_pairs(clean)
    let lines = split(clean, "\n")
    var starts = []
    var next_start = 0
    for (line in lines) {
        push(starts, next_start)
        next_start = next_start + len(line) + 1
    }
    var functions = []
    var offset = 0
    for (line in lines) {
        var cursor = 0
        while (cursor < len(line)) {
            let found = c_find(line, "{", cursor)
            if (found == null) { break }
            cursor = found
            let brace = offset + cursor
            cursor = cursor + 1
            let window_start = max(0, brace - 160)
            let window = c_segment(lines, starts, window_start, brace)
            var close_local = len(window) - 1
            while (close_local >= 0 and c_scan_space(slice(window, close_local, close_local + 1))) {
                close_local = close_local - 1
            }
            if (close_local < 0 or slice(window, close_local, close_local + 1) != ")") {
                close_local = len(window) - 1
                while (close_local >= 0 and slice(window, close_local, close_local + 1) != ")") {
                    close_local = close_local - 1
                }
                if (close_local < 0) { continue }
                let suffix = slice(window, close_local + 1)
                if (contains(suffix, ";") or contains(suffix, "=") or
                    contains(suffix, "{") or contains(suffix, "}") or
                    contains(suffix, "[") or contains(suffix, "]")) {
                    continue
                }
            }
            let close_paren = window_start + close_local
            let open_paren = c_pair_lookup(pairs.parens, close_paren)
            if (open_paren < 0) { continue }
            let name_window_start = max(0, open_paren - 180)
            let name_window = c_segment(lines, starts, name_window_start, open_paren)
            let named = c_scan_name(name_window, len(name_window))
            if (named == null or c_scan_non_call(named.name) or
                starts_with(named.name, "operator")) { continue }
            let name_start = name_window_start + named.start
            let boundary = c_previous_boundary(lines, starts, name_start)
            if (c_scan_prefix_rejected(c_segment(lines, starts, boundary + 1, name_start))) {
                continue
            }
            let body_end = c_pair_lookup(pairs.braces, brace)
            if (body_end < 0) { continue }
            push(functions, {name: named.name, path: path,
                             line: c_line_index(starts, name_start) + 1,
                             body: c_segment(lines, starts, brace + 1, body_end)})
        }
        offset = offset + len(line) + 1
    }
    return functions
}

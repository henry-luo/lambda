// Source scans for the static-module ownership inventory.

fn ident_char(ch: string) bool =>
    len(ch) == 1 and contains("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_", ch)
fn white(ch: string) bool =>
    len(ch) == 1 and (contains(" \t\r\n", ch) or ch == chr(11) or ch == chr(12))

pub fn c_source(name: string) bool =>
    ends_with(name, ".c") or ends_with(name, ".cc") or
    ends_with(name, ".cpp") or ends_with(name, ".h") or ends_with(name, ".hpp")

fn runtime_marker(token: string) bool =>
    contains(["Context", "EvalContext", "RootFrame", "Rooted", "NoGC", "g_dry_run"], token) or
    starts_with(token, "heap_") or starts_with(token, "gc_") or
    starts_with(token, "lambda_root_") or starts_with(token, "lambda_stack_")

fn io_marker(token: string) bool =>
    contains(["Input", "Target", "Url", "read_text_file", "write_text_file"], token) or
    starts_with(token, "target_") or starts_with(token, "curl_") or
    starts_with(token, "resource_") or starts_with(token, "network_")

fn direct_io_name(token: string) bool =>
    starts_with(token, "curl_") or starts_with(token, "uv_") or
    contains(["open", "read", "write", "socket", "connect"], token)

pub pn identifier_matches(source: string, kind: string) {
    var matches = []
    var line_line_number = 1
    for (line in split(source, "\n")) {
        var cursor = 0
        while (cursor < len(line)) {
            let ch = slice(line, cursor, cursor + 1)
            if (ident_char(ch)) {
                let start = cursor
                while (cursor < len(line) and ident_char(slice(line, cursor, cursor + 1))) {
                    cursor = cursor + 1
                }
                let name = slice(line, start, cursor)
                if (kind == "runtime" and runtime_marker(name)) {
                    matches = matches ++ [{line: line_line_number, token: name}]
                }
                if (kind == "io" and io_marker(name)) {
                    matches = matches ++ [{line: line_line_number, token: name}]
                }
                if (kind == "direct_io" and direct_io_name(name)) {
                    var tail = cursor
                    while (tail < len(line) and white(slice(line, tail, tail + 1))) {
                        tail = tail + 1
                    }
                    if (slice(line, tail, tail + 1) == "(") {
                        matches = matches ++ [{line: line_line_number,
                                               token: slice(line, start, tail + 1)}]
                    }
                }
            } else {
                cursor = cursor + 1
            }
        }
        line_line_number = line_line_number + 1
    }
    return matches
}

pn skip_space(line: string, start: int) {
    var cursor = start
    while (cursor < len(line) and white(slice(line, cursor, cursor + 1))) {
        cursor = cursor + 1
    }
    return cursor
}

pub pn include_matches(source: string, target: string) {
    var matches = []
    var line_number = 1
    for (line in split(source, "\n")) {
        var cursor = skip_space(line, 0)
        if (slice(line, cursor, cursor + 1) == "#") {
            cursor = skip_space(line, cursor + 1)
            if (starts_with(slice(line, cursor), "include") and
                white(slice(line, cursor + 7, cursor + 8))) {
                cursor = skip_space(line, cursor + 7)
                let quote = slice(line, cursor, cursor + 1)
                if (quote == "\"" or quote == "<") {
                    let close = if (quote == "\"") "\"" else ">"
                    let path_end = index_of(slice(line, cursor + 1), close)
                    if (path_end != null) {
                        let prefix = slice(line, cursor + 1, cursor + 1 + path_end)
                        let hit = index_of(prefix, target ++ "/")
                        if (hit != null) {
                            let end = cursor + 1 + hit + len(target) + 1
                            matches = matches ++ [{line: line_number, token: trim(slice(line, 0, end))}]
                        }
                    }
                }
            }
        }
        line_number = line_number + 1
    }
    return matches
}

// Accept both spelling variants of __attribute__((weak)).
pub pn weak_matches(source: string) {
    var matches = []
    var line_number = 1
    for (line in split(source, "\n")) {
        var offset = 0
        while (offset < len(line)) {
            let hit = index_of(slice(line, offset), "__attribute__")
            if (hit == null) { break }
            let start = offset + hit
            offset = start + len("__attribute__")
            var cursor = skip_space(line, offset)
            if (slice(line, cursor, cursor + 1) == "(") {
                cursor = skip_space(line, cursor + 1)
                if (slice(line, cursor, cursor + 1) == "(" and
                    starts_with(slice(line, cursor + 1), "weak)")) {
                    cursor = skip_space(line, cursor + 6)
                    if (slice(line, cursor, cursor + 1) == ")") {
                        matches = matches ++ [{line: line_number, token: slice(line, start, cursor + 1)}]
                    }
                }
            }
        }
        line_number = line_number + 1
    }
    return matches
}

pub pn upward_extern_matches(source: string) {
    var matches = []
    var line_number = 1
    for (line in split(source, "\n")) {
        let stripped = trim(line)
        if (starts_with(stripped, "extern") and white(slice(stripped, 6, 7))) {
            let semicolon = index_of(stripped, ";")
            if (semicolon != null) {
                let declaration = slice(stripped, 0, semicolon + 1)
                var found = false
                for (name in ["heap_alloc", "heap_data_alloc", "dispatch_emit",
                              "counter_format", "resolve_symbol", "resolve_symbol_string",
                              "log_mem_stage"]) {
                    if (contains(declaration, name)) { found = true }
                }
                if (contains(declaration, "lambda_root_") or
                    contains(declaration, "lambda_weak_")) { found = true }
                if (found) { matches = matches ++ [{line: line_number, declaration: declaration}] }
            }
        }
        line_number = line_number + 1
    }
    return matches
}

pub pn static_selector_matches(source: string) {
    var matches = []
    var line_number = 1
    for (line in split(source, "\n")) {
        var cursor = skip_space(line, 0)
        if (slice(line, cursor, cursor + 1) == "#") {
            cursor = skip_space(line, cursor + 1)
            let op = if (starts_with(slice(line, cursor), "define")) "define"
                     else if (starts_with(slice(line, cursor), "undef")) "undef" else ""
            if (op != "") {
                cursor = skip_space(line, cursor + len(op))
                if (starts_with(slice(line, cursor), "LAMBDA_STATIC") and
                    not ident_char(slice(line, cursor + 13, cursor + 14))) {
                    matches = matches ++ [{line: line_number, token: trim(slice(line, 0, cursor + 13))}]
                }
            }
        }
        line_number = line_number + 1
    }
    return matches
}

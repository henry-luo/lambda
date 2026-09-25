// Character-level matchers for the source-only JS callable census.
fn callable_ident(ch: string) bool =>
    len(ch) == 1 and contains("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_", ch)
fn callable_space(ch: string) bool =>
    len(ch) == 1 and contains(" \t\r\n", ch)

pub pn callable_skip_space(source: string, offset: int) {
    var at = offset
    while (at < len(source) and callable_space(slice(source, at, at + 1))) { at = at + 1 }
    return at
}

pub pn callable_word_positions(source: string, name: string) {
    var hits = []
    var from = 0
    while (from < len(source)) {
        let found = index_of(slice(source, from), name)
        if (found == null) { break }
        let at = from + found
        if ((at == 0 or not callable_ident(slice(source, at - 1, at))) and
            not callable_ident(slice(source, at + len(name), at + len(name) + 1))) {
            hits = hits ++ [at]
        }
        from = at + len(name)
    }
    return hits
}

pub pn callable_count_word(source: string, name: string) {
    return len(callable_word_positions(source, name))
}

pub pn callable_count_call(source: string, name: string) {
    var count = 0
    for (at in callable_word_positions(source, name)) {
        let tail = callable_skip_space(source, at + len(name))
        if (slice(source, tail, tail + 1) == "(") { count = count + 1 }
    }
    return count
}

pub pn callable_count_union(source: string, names, calls: bool) {
    var count = 0
    for (name in names) {
        count = count + if (calls) callable_count_call(source, name)
                        else callable_count_word(source, name)
    }
    return count
}

pub pn callable_count_decl(source: string, name: string) {
    var count = 0
    for (line in split(source, "\n")) {
        var tail = trim(line)
        if (starts_with(tail, "static")) {
            let at = callable_skip_space(tail, 6)
            if (at > 6) { tail = slice(tail, at) }
        }
        if (starts_with(tail, "Item")) {
            let at = callable_skip_space(tail, 4)
            if (at > 4 and starts_with(slice(tail, at), name)) {
                let end = callable_skip_space(tail, at + len(name))
                if (slice(tail, end, end + 1) == "(") { count = count + 1 }
            }
        }
    }
    return count
}

pub pn callable_count_static_decl(source: string, name: string) {
    var count = 0
    for (line in split(source, "\n")) {
        let tail = trim(line)
        if (starts_with(tail, "static")) {
            let at = callable_skip_space(tail, 6)
            if (at > 6) { count = count + callable_count_decl(slice(tail, at), name) }
        }
    }
    return count
}

pub pn callable_count_prefixed_word(source: string, prefix: string) {
    var count = 0
    var from = 0
    while (from < len(source)) {
        let found = index_of(slice(source, from), prefix)
        if (found == null) { break }
        let at = from + found
        let after = at + len(prefix)
        if ((at == 0 or not callable_ident(slice(source, at - 1, at))) and
            callable_ident(slice(source, after, after + 1))) {
            count = count + 1
        }
        from = after
    }
    return count
}

pub pn callable_count_prefixed_call(source: string, prefix: string) {
    var count = 0
    var from = 0
    while (from < len(source)) {
        let found = index_of(slice(source, from), prefix)
        if (found == null) { break }
        let at = from + found
        var end = at + len(prefix)
        while (callable_ident(slice(source, end, end + 1))) { end = end + 1 }
        if ((at == 0 or not callable_ident(slice(source, at - 1, at))) and
            end > at + len(prefix) and
            slice(source, callable_skip_space(source, end),
                  callable_skip_space(source, end) + 1) == "(") {
            count = count + 1
        }
        from = end
    }
    return count
}

pub pn callable_count_builtin_cases(source: string) {
    var count = 0
    for (at in callable_word_positions(source, "case")) {
        let start = callable_skip_space(source, at + 4)
        if (starts_with(slice(source, start), "JS_BUILTIN_")) {
            var end = start + len("JS_BUILTIN_")
            while (callable_ident(slice(source, end, end + 1))) { end = end + 1 }
            end = callable_skip_space(source, end)
            if (slice(source, end, end + 1) == ":") { count = count + 1 }
        }
    }
    return count
}

pub pn callable_count_factory_casts(source: string) {
    var count = 0
    for (at in callable_word_positions(source, "js_new_function")) {
        var tail = callable_skip_space(source, at + len("js_new_function"))
        if (slice(source, tail, tail + 1) == "(") {
            tail = callable_skip_space(source, tail + 1)
            if (slice(source, tail, tail + 1) == "(") {
                tail = callable_skip_space(source, tail + 1)
                if (starts_with(slice(source, tail), "void")) {
                    tail = callable_skip_space(source, tail + 4)
                    if (slice(source, tail, tail + 1) == "*") {
                        tail = callable_skip_space(source, tail + 1)
                        if (slice(source, tail, tail + 1) == ")") { count = count + 1 }
                    }
                }
            }
        }
    }
    return count
}

pub pn callable_count_catalog_reads(source: string) {
    var count = 0
    for (at in callable_word_positions(source, "fn")) {
        if (starts_with(slice(source, at + 2), "->catalog_id")) {
            let end = at + len("fn->catalog_id")
            if (not callable_ident(slice(source, end, end + 1)) and
                slice(source, callable_skip_space(source, end),
                      callable_skip_space(source, end) + 1) != "=") {
                count = count + 1
            }
        }
    }
    return count
}

pub pn callable_count_constructor_names(source: string) {
    var count = 0
    for (name in ["strncmp", "memcmp"]) {
        for (at in callable_word_positions(source, name)) {
            var cursor = callable_skip_space(source, at + len(name))
            if (slice(source, cursor, cursor + 1) == "(") {
                cursor = cursor + 1
                while (cursor < len(source) and
                       slice(source, cursor, cursor + 1) != "," and
                       slice(source, cursor, cursor + 1) != "\n") {
                    cursor = cursor + 1
                }
                if (slice(source, cursor, cursor + 1) == ",") {
                    cursor = callable_skip_space(source, cursor + 1)
                    if (slice(source, cursor, cursor + 1) == "\"") { count = count + 1 }
                }
            }
        }
    }
    return count
}

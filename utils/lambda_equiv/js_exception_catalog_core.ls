// Native source census for the D8.4.3 JS exception-effect contract.
import .path_utils

fn ident_char(ch: string) bool =>
    len(ch) == 1 and contains("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_", ch)
fn white(ch: string) bool =>
    len(ch) == 1 and (contains(" \t\r\n", ch) or ch == chr(11) or ch == chr(12))

pn skip_space(source: string, start: int) {
    var cursor = start
    while (cursor < len(source) and white(slice(source, cursor, cursor + 1))) {
        cursor = cursor + 1
    }
    return cursor
}

pn identifier(source: string, start: int) {
    var cursor = start
    while (cursor < len(source) and ident_char(slice(source, cursor, cursor + 1))) {
        cursor = cursor + 1
    }
    return {text: slice(source, start, cursor), next: cursor}
}

pn registry_row(source: string, start: int) {
    let name = identifier(source, start + 2)
    if (name.text == "" or slice(source, name.next, name.next + 2) != "\",") {
        return null
    }
    var cursor = skip_space(source, name.next + 2)
    if (not starts_with(slice(source, cursor), "FPTR(")) { return null }
    let target = identifier(source, cursor + 5)
    if (target.text == "" or slice(source, target.next, target.next + 1) != ")") {
        return null
    }
    return {name: name.text, target: target.text, start: start,
            after_fptr: target.next + 1}
}

pn return_class(body: string, after_fptr: int, row_start: int) {
    var cursor = skip_space(body, after_fptr - row_start)
    if (slice(body, cursor, cursor + 1) != ",") { return "JIT_VALUE_UNKNOWN" }
    cursor = skip_space(body, cursor + 1)
    if (slice(body, cursor, cursor + 1) != "{") { return "JIT_VALUE_UNKNOWN" }
    cursor = cursor + 1
    var commas = 0
    while (cursor < len(body) and commas < 2) {
        if (slice(body, cursor, cursor + 1) == ",") { commas = commas + 1 }
        cursor = cursor + 1
    }
    cursor = skip_space(body, cursor)
    if (not starts_with(slice(body, cursor), "JIT_VALUE_")) {
        return "JIT_VALUE_UNKNOWN"
    }
    return identifier(body, cursor).text
}

pub pn registry_rows(source: string) {
    var locations = []
    let fragments = split(source, "{\"")
    var start = len(fragments[0])
    var part = 1
    while (part < len(fragments)) {
        let row = registry_row(source, start)
        if (row != null) { locations = locations ++ [row] }
        start = start + 2 + len(fragments[part])
        part = part + 1
    }
    var rows = []
    var i = 0
    while (i < len(locations)) {
        let row = locations[i]
        let end = if (i + 1 < len(locations)) locations[i + 1].start else len(source)
        let body = slice(source, row.start, end)
        var effect = "MAY_SET(default)"
        for (candidate in ["PRESERVES", "CLEARS", "SETS"]) {
            if (effect == "MAY_SET(default)" and
                contains(body, "JIT_EXCEPTION_" ++ candidate)) {
                effect = candidate
            }
        }
        var ret_class = return_class(body, row.after_fptr, row.start)
        if (contains(body, "JIT_IMPORT_RAW_SCALAR_PRESERVES") or
            contains(body, "JIT_IMPORT_PURE_SCALAR")) {
            ret_class = "JIT_VALUE_NON_GC_SCALAR"
            effect = "PRESERVES"
        }
        if (contains(body, "JIT_IMPORT_VOID_PRESERVES")) { effect = "PRESERVES" }
        rows = rows ++ [{name: row.name, target: row.target, effect: effect,
                         ret_class: ret_class}]
        i = i + 1
    }
    // Registry conditionals may repeat a name; Python's dict keeps its last row.
    var by_name = {}
    var names = []
    for (row in rows) {
        if (by_name[row.name] == null) { names = names ++ [row.name] }
        by_name[row.name] = row
    }
    return [for (name in names) by_name[name]]
}

pn return_declaration(source: string, start: int) {
    var cursor = skip_space(source, start + len("extern"))
    if (not starts_with(slice(source, cursor), "\"C\"")) { return null }
    cursor = skip_space(source, cursor + 3)
    let ret = identifier(source, cursor)
    if (ret.text == "") { return null }
    cursor = skip_space(source, ret.next)
    let name = identifier(source, cursor)
    if (name.text == "") { return null }
    cursor = skip_space(source, name.next)
    if (slice(source, cursor, cursor + 1) != "(") { return null }
    return {name: name.text, ret: ret.text}
}

pn scan_return_types(path: string, var types) any^ {
    let source = input(path, "text")^
    let fragments = split(source, "extern")
    var position = len(fragments[0])
    var i = 1
    while (i < len(fragments)) {
        let declaration = return_declaration(source, position)
        if (declaration != null and types[declaration.name] == null) {
            types[declaration.name] = declaration.ret
        }
        position = position + len("extern") + len(fragments[i])
        i = i + 1
    }
}

// Python's HELPER_GLOBS visits each sorted extension group before the next.
pub pn helper_paths() {
    var js_cpp = []
    var js_c = []
    var runtime_cpp = []
    var runtime_c = []
    for (path in \.lambda.js.*) {
        if (path.is_file) {
            let rel = relative_path(path)
            if (ends_with(path.name, ".cpp")) { js_cpp = js_cpp ++ [rel] }
            else if (ends_with(path.name, ".c")) { js_c = js_c ++ [rel] }
        }
    }
    for (path in \.lambda.runtime.*) {
        if (path.is_file) {
            let rel = relative_path(path)
            if (ends_with(path.name, ".cpp")) { runtime_cpp = runtime_cpp ++ [rel] }
            else if (ends_with(path.name, ".c")) { runtime_c = runtime_c ++ [rel] }
        }
    }
    return sort(js_cpp) ++ sort(js_c) ++ sort(runtime_cpp) ++ sort(runtime_c)
}

pub pn return_types() any^ {
    var types = {}
    for (path in helper_paths()) { scan_return_types(path, types)^ }
    return types
}

// Sort string keys directly because sort(rows, ~.name) currently ignores its key.
pub pn rows_by_name(rows) {
    var by_name = {}
    var names = []
    for (row in rows) {
        names = names ++ [row.name]
        by_name[row.name] = row
    }
    return [for (name in sort(names)) by_name[name]]
}

pub pn census(prefix) any^ {
    let registry = input("lambda/runtime/sys_func_registry.c", "text")^
    let all_rows = registry_rows(registry)
    let types = return_types()^
    var selected = []
    var tier_a = []
    var tier_b = []
    var tier_c = []
    for (row in all_rows) {
        if (prefix == null or starts_with(row.name, prefix)) {
            selected = selected ++ [row]
            let ret = types[row.target]
            if (ret == "Item" and row.ret_class == "JIT_VALUE_NON_GC_SCALAR") {
                tier_c = tier_c ++ [{name: row.name, target: row.target,
                                     ret: ret, value: row.ret_class}]
            } else if (ret == "void" and row.effect != "PRESERVES") {
                tier_b = tier_b ++ [{name: row.name, target: row.target,
                                     ret: ret, value: row.effect}]
            } else if (ret != null and ret != "Item" and ret != "void" and
                       (row.ret_class != "JIT_VALUE_NON_GC_SCALAR" or
                        row.effect != "PRESERVES")) {
                tier_a = tier_a ++ [{name: row.name, target: row.target,
                                     ret: ret, value: row.effect}]
            }
        }
    }
    return {selected: selected, tier_a: rows_by_name(tier_a),
            tier_b: rows_by_name(tier_b), tier_c: rows_by_name(tier_c),
            helper_count: len(helper_paths())}
}

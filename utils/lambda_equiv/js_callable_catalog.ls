// Catalog row parsing and validation for the JS callable census.

pub pn callable_macro_rows(source: string, macro: string) {
    let marker = macro ++ "("
    var rows = []
    var offset = 0
    while (offset < len(source)) {
        let found = index_of(slice(source, offset), marker)
        if (found == null) { break }
        let start = offset + found + len(marker)
        var cursor = start
        var depth = 1
        var quoted = false
        var escaped = false
        while (cursor < len(source) and depth > 0) {
            let ch = slice(source, cursor, cursor + 1)
            if (quoted) {
                if (escaped) { escaped = false }
                else if (ch == "\\") { escaped = true }
                else if (ch == "\"") { quoted = false }
            } else if (ch == "\"") { quoted = true }
            else if (ch == "(") { depth = depth + 1 }
            else if (ch == ")") { depth = depth - 1 }
            cursor = cursor + 1
        }
        if (depth > 0) {
            rows = rows ++ [["<unterminated>"]]
            break
        }
        let body = slice(source, start, cursor - 1)
        var args = []
        var arg_start = 0
        var nesting = 0
        quoted = false
        escaped = false
        var at = 0
        while (at < len(body)) {
            let ch = slice(body, at, at + 1)
            if (quoted) {
                if (escaped) { escaped = false }
                else if (ch == "\\") { escaped = true }
                else if (ch == "\"") { quoted = false }
            } else if (ch == "\"") { quoted = true }
            else if (ch == "(") { nesting = nesting + 1 }
            else if (ch == ")") { nesting = nesting - 1 }
            else if (ch == "," and nesting == 0) {
                args = args ++ [trim(slice(body, arg_start, at))]
                arg_start = at + 1
            }
            at = at + 1
        }
        args = args ++ [trim(slice(body, arg_start))]
        rows = rows ++ [args]
        offset = cursor
    }
    return rows
}

pn callable_quoted(value: string) {
    if (len(value) < 2 or slice(value, 0, 1) != "\"" or
        slice(value, len(value) - 1) != "\"") { return null }
    return parse(value, "json") ^ { null }
}

pn callable_length(value: string) {
    return int(value) ^ { null }
}

pub pn callable_catalog_errors(source: string) {
    var errors = []
    var owners = {}
    for (row in callable_macro_rows(source, "JS_BUILTIN_OWNER")) {
        if (len(row) == 1) { owners[row[0]] = true }
    }
    var targets = {}
    for (spec in [{name: "JS_BUILTIN_ID", width: 3},
                  {name: "JS_BUILTIN_CONSTRUCTOR_TARGET", width: 4}]) {
        for (row in callable_macro_rows(source, spec.name)) {
            if (len(row) != spec.width) {
                errors = errors ++ [spec.name ++ ": expected " ++ string(spec.width) ++
                                    " arguments, got " ++ string(len(row))]
            } else {
                let id = row[0]
                let call_body = row[1]
                let construct_body = if (spec.width == 4) row[2] else "NULL"
                if (targets[id] != null) {
                    errors = errors ++ ["duplicate target id " ++ id]
                } else {
                    if (call_body == "NULL" and construct_body == "NULL") {
                        errors = errors ++ ["target " ++ id ++ " has no call or construct body"]
                    }
                    targets[id] = {call_body: call_body, construct_body: construct_body}
                }
            }
        }
    }

    var seen_bindings = {}
    var aliases = {}
    for (row in callable_macro_rows(source, "JS_BUILTIN_METHOD")) {
        if (len(row) != 9) {
            errors = errors ++ ["JS_BUILTIN_METHOD: expected 9 arguments, got " ++ string(len(row))]
            continue
        }
        let owner = row[0]
        let name_literal = row[1]
        let length = row[2]
        let target_id = row[3]
        let arity = row[4]
        let display_name = row[5]
        let prop_kind = row[6]
        let flags = row[7]
        let alias = row[8]
        let name = callable_quoted(name_literal)
        if (owners[owner] == null) {
            errors = errors ++ ["binding " ++ name_literal ++ " has unknown owner " ++ owner]
        }
        if (name == null) {
            errors = errors ++ ["binding has invalid name literal " ++ name_literal]
            continue
        }
        let declared = callable_length(length)
        if (declared == null) {
            errors = errors ++ ["binding " ++ owner ++ "." ++ name ++
                                " has non-integer length " ++ length]
        } else if (declared != len(name)) {
            errors = errors ++ ["binding " ++ owner ++ "." ++ name ++
                                " has mismatched length " ++ length]
        }
        let key = owner ++ "\u0000" ++ name
        if (seen_bindings[key] != null) {
            errors = errors ++ ["duplicate owner/property binding " ++ owner ++ "." ++ name]
        }
        seen_bindings[key] = true
        if (target_id != "JS_BUILTIN_NONE" and targets[target_id] == null) {
            errors = errors ++ ["binding " ++ owner ++ "." ++ name ++
                                " references missing target " ++ target_id]
        }
        if (alias != "JS_INTRINSIC_ALIAS_NONE") {
            let observable = if (display_name == "NULL") name_literal else display_name
            let signature = [target_id, arity, observable, prop_kind, flags]
            if (aliases[alias] != null and aliases[alias] != signature) {
                errors = errors ++ ["identity alias " ++ alias ++ " has incompatible bindings"]
            }
            aliases[alias] = signature
        }
    }

    var seen_ids = {}
    var seen_names = {}
    for (row in callable_macro_rows(source, "JS_BUILTIN_GLOBAL")) {
        if (len(row) != 8) {
            errors = errors ++ ["JS_BUILTIN_GLOBAL: expected 8 arguments, got " ++ string(len(row))]
            continue
        }
        let global_id = row[0]
        let name_literal = row[1]
        let length = row[2]
        let kind = row[3]
        let runtime_id = row[4]
        let target_id = row[5]
        let name = callable_quoted(name_literal)
        if (name == null) {
            errors = errors ++ ["global has invalid name literal " ++ name_literal]
            continue
        }
        let declared = callable_length(length)
        if (declared == null) {
            errors = errors ++ ["global " ++ name ++ " has non-integer length " ++ length]
        } else if (declared != len(name)) {
            errors = errors ++ ["global " ++ name ++ " has mismatched length " ++ length]
        }
        if (seen_ids[global_id] != null) { errors = errors ++ ["duplicate global id " ++ global_id] }
        if (seen_names[name] != null) { errors = errors ++ ["duplicate global name " ++ name] }
        seen_ids[global_id] = true
        seen_names[name] = true
        if (kind == "JS_BUILTIN_GLOBAL_NAMESPACE") {
            if (target_id != "JS_BUILTIN_NONE") {
                errors = errors ++ ["namespace " ++ name ++ " unexpectedly has target " ++ target_id]
            }
            continue
        }
        let target = targets[target_id]
        if (target == null) {
            errors = errors ++ ["global " ++ name ++ " references missing target " ++ target_id]
            continue
        }
        if (target.call_body == "NULL") { errors = errors ++ ["global " ++ name ++ " has no call body"] }
        if (kind == "JS_BUILTIN_GLOBAL_FUNCTION" and target.construct_body != "NULL") {
            errors = errors ++ ["global function " ++ name ++ " unexpectedly has construct body"]
        } else if (kind == "JS_BUILTIN_GLOBAL_CONSTRUCTOR") {
            if (target.construct_body == "NULL") {
                errors = errors ++ ["constructor binding " ++ name ++ " has no construct body"]
            } else if (contains(["JS_CTOR_SYMBOL", "JS_CTOR_BIGINT"], runtime_id) and
                       target.construct_body != "js_intrinsic_ctor_forbidden_construct_body") {
                errors = errors ++ ["rejecting constructor binding " ++ name ++
                                    " has wrong construct body"]
            }
        }
    }
    return errors
}

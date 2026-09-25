// Native source parser for Radiant's declarative state schema.
import .c_text

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

pub pn parse_enums(header: string) {
    var enums = {}
    var offset = 0
    while (offset < len(header)) {
        let hit = index_of(slice(header, offset), "typedef")
        if (hit == null) { break }
        let start = offset + hit
        offset = start + len("typedef")
        var cursor = skip_space(header, offset)
        if (starts_with(slice(header, cursor), "enum") and
            white(slice(header, cursor + 4, cursor + 5))) {
            cursor = skip_space(header, cursor + 4)
            let name = identifier(header, cursor)
            cursor = skip_space(header, name.next)
            if (name.text != "" and slice(header, cursor, cursor + 1) == "{") {
                let close = index_of(slice(header, cursor + 1), "}")
                if (close != null) {
                    let body = strip_c_comments(slice(header, cursor + 1, cursor + 1 + close))
                    var values = []
                    for (raw in split(body, ",")) {
                        let entry = trim(split(trim(raw), "=")[0])
                        if (entry != "") { values = values ++ [entry] }
                    }
                    let alias = identifier(header, skip_space(header, cursor + 2 + close))
                    if (alias.text != "" and
                        slice(header, skip_space(header, alias.next),
                              skip_space(header, alias.next) + 1) == ";") {
                        enums[name.text] = values
                    }
                    offset = cursor + 2 + close
                }
            }
        }
    }
    return enums
}

pub pn csv_fields(source: string) {
    var fields = []
    var start = 0
    var depth = 0
    var quoted = false
    var escaped = false
    var i = 0
    while (i < len(source)) {
        let ch = slice(source, i, i + 1)
        if (quoted) {
            if (escaped) { escaped = false }
            else if (ch == "\\") { escaped = true }
            else if (ch == "\"") { quoted = false }
        } else if (ch == "\"") {
            quoted = true
        } else if (ch == "(") {
            depth = depth + 1
        } else if (ch == ")") {
            depth = depth - 1
        } else if (ch == "," and depth == 0) {
            fields = fields ++ [trim(slice(source, start, i))]
            start = i + 1
        }
        i = i + 1
    }
    let tail = trim(slice(source, start))
    if (tail != "") { fields = fields ++ [tail] }
    return fields
}

pub pn parse_to_state_arrays(source: string) {
    var arrays = {}
    var offset = 0
    let marker = "static const int"
    while (offset < len(source)) {
        let hit = index_of(slice(source, offset), marker)
        if (hit == null) { break }
        let start = offset + hit
        offset = start + len(marker)
        var cursor = skip_space(source, offset)
        let name = identifier(source, cursor)
        cursor = skip_space(source, name.next)
        if (name.text != "" and starts_with(slice(source, cursor), "[]")) {
            cursor = skip_space(source, cursor + 2)
            if (slice(source, cursor, cursor + 1) == "=") {
                cursor = skip_space(source, cursor + 1)
                if (slice(source, cursor, cursor + 1) == "{") {
                    let close = index_of(slice(source, cursor + 1), "};")
                    if (close != null) {
                        let body = strip_c_comments(slice(source, cursor + 1, cursor + 1 + close))
                        arrays[name.text] = csv_fields(body)
                        offset = cursor + 1 + close + 2
                    }
                }
            }
        }
    }
    return arrays
}

pn table_body(source: string, marker: string, missing: string) string^ {
    let found = index_of(source, marker)
    if (found == null) { raise error("missing " ++ missing ++ " table") }
    let start = found + len(marker)
    let close = index_of(slice(source, start), "\n};")
    if (close == null) { raise error("missing end of " ++ missing ++ " table") }
    return slice(source, start, start + close)
}

pub pn entry_bodies(source: string) {
    var entries = []
    var depth = 0
    var start = null
    var quoted = false
    var escaped = false
    var i = 0
    while (i < len(source)) {
        let ch = slice(source, i, i + 1)
        if (quoted) {
            if (escaped) { escaped = false }
            else if (ch == "\\") { escaped = true }
            else if (ch == "\"") { quoted = false }
        } else if (ch == "\"") {
            quoted = true
        } else if (ch == "{") {
            if (depth == 0) { start = i + 1 }
            depth = depth + 1
        } else if (ch == "}") {
            depth = depth - 1
            if (depth == 0 and start != null) {
                entries = entries ++ [slice(source, start, i)]
                start = null
            }
        }
        i = i + 1
    }
    return entries
}

fn unquote(value: string) =>
    if (starts_with(value, "\"") and ends_with(value, "\""))
        slice(value, 1, len(value) - 1) else value

pub pn parse_rules(source: string, arrays) any^ {
    let body = table_body(source, "static const StateTransitionRule RADIANT_STATE_RULES[] = {",
                          "RADIANT_STATE_RULES[]")^
    var rules = []
    for (entry in entry_bodies(body)) {
        let fields = csv_fields(strip_c_comments(entry))
        if (len(fields) != 10) {
            raise error("rule has " ++ string(len(fields)) ++ " field(s), expected 10: " ++
                        join(split(trim(entry), null), " "))
        }
        let to_expr = trim(fields[5])
        if (not starts_with(to_expr, "SM_RULE_TO") or not ends_with(to_expr, ")")) {
            raise error("unsupported to-state expression: " ++ to_expr)
        }
        let opening = index_of(to_expr, "(")
        if (opening == null) { raise error("unsupported to-state expression: " ++ to_expr) }
        let array_name = trim(slice(to_expr, opening + 1, len(to_expr) - 1))
        let states = arrays[array_name]
        if (states == null) { raise error("unknown to-state array: " ++ array_name) }
        rules = rules ++ [{family: fields[0], view_class: fields[1], from_state: fields[2],
                           event: fields[3], guard: fields[4], to_states: states,
                           to_array: array_name, actions: fields[6],
                           invariants: fields[7], invariant_count: fields[8],
                           name: unquote(fields[9])}]
    }
    return rules
}

pub pn parse_invariant_bindings(source: string) any^ {
    let body = table_body(source, "RADIANT_INVARIANTS[] = {", "RADIANT_INVARIANTS[]")^
    var bindings = []
    for (entry in entry_bodies(body)) {
        let fields = csv_fields(strip_c_comments(entry))
        if (len(fields) != 4) {
            raise error("invariant binding has " ++ string(len(fields)) ++
                        " field(s), expected 4: " ++ join(split(trim(entry), null), " "))
        }
        bindings = bindings ++ [{family: fields[0], state: fields[1],
                                 invariant: fields[2], name: unquote(fields[3])}]
    }
    return bindings
}

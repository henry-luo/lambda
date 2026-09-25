// Native Lambda extraction of fenced and annotated-table units (Doc_Convention §8.1).
import .path_utils

fn white(ch: string) bool =>
    len(ch) == 1 and (contains(" \t\r\n", ch) or ch == chr(11) or ch == chr(12))

pn words(source: string) {
    var result = []
    var cursor = 0
    while (cursor < len(source)) {
        while (cursor < len(source) and white(slice(source, cursor, cursor + 1))) {
            cursor = cursor + 1
        }
        let start = cursor
        while (cursor < len(source) and not white(slice(source, cursor, cursor + 1))) {
            cursor = cursor + 1
        }
        if (start < cursor) { result = result ++ [slice(source, start, cursor)] }
    }
    return result
}

pn table_directives(raw: string) {
    if (not starts_with(raw, "<!--") or not ends_with(trim(raw), "-->")) { return null }
    let inner = trim(slice(trim(raw), 4, len(trim(raw)) - 3))
    if (not starts_with(inner, "code-fence:")) { return null }
    let value = trim(slice(inner, len("code-fence:")))
    if (value == "lambda") { return [] }
    if (starts_with(value, "lambda") and white(slice(value, 6, 7))) {
        return words(slice(value, 6))
    }
    return null
}

pn fence_directives(stripped: string) {
    if (stripped == "```lambda") { return [] }
    if (starts_with(stripped, "```lambda") and white(slice(stripped, 9, 10))) {
        return words(slice(stripped, 9))
    }
    return null
}

// A preceding backslash protects a pipe, as in the Python regex split.
pn table_cells(raw: string) {
    var line = trim(raw)
    while (starts_with(line, "|")) { line = slice(line, 1) }
    while (ends_with(line, "|")) { line = slice(line, 0, len(line) - 1) }
    var cells = []
    var start = 0
    var pos = 0
    while (pos < len(line)) {
        let ch = slice(line, pos, pos + 1)
        if (ch == "|" and (pos == 0 or slice(line, pos - 1, pos) != "\\")) {
            cells = cells ++ [trim(slice(line, start, pos))]
            start = pos + 1
        }
        pos = pos + 1
    }
    return cells ++ [trim(slice(line, start))]
}

pn table_code(raw: string) {
    let cells = table_cells(raw)
    if (len(cells) == 0) { return null }
    let first = cells[0]
    if (len(first) <= 2 or not starts_with(first, "`") or
        not ends_with(first, "`")) { return null }
    let middle = slice(first, 1, len(first) - 1)
    if (contains(middle, "`")) { return null }
    return replace(middle, "\\|", "|")
}

pub pn scan_doc(path: string) any^ {
    let source = input(path, "text")^
    let lines = split(replace(replace(source, "\r\n", "\n"), "\r", "\n"), "\n")
    var units = []
    var pending = null
    var i = 0
    while (i < len(lines)) {
        let raw = lines[i]
        let stripped = trim(raw)
        let meta = table_directives(raw)
        if (meta != null) {
            pending = meta
            i = i + 1
        } else if (pending != null and starts_with(raw, "|")) {
            var row = 0
            while (i < len(lines) and starts_with(lines[i], "|")) {
                if (row >= 2) {
                    let code = table_code(lines[i])
                    if (code != null) {
                        units = units ++ [{line: i + 1, directives: pending, code: code}]
                    }
                }
                i = i + 1
                row = row + 1
            }
            pending = null
        } else {
            let directives = fence_directives(stripped)
            if (directives != null) {
                let start = i + 1
                var body = []
                i = i + 1
                while (i < len(lines) and not starts_with(trim(lines[i]), "```")) {
                    body = body ++ [lines[i]]
                    i = i + 1
                }
                i = i + 1
                units = units ++ [{line: start, directives: directives, code: join(body, "\n")}]
            } else {
                if (stripped != "") { pending = null }
                i = i + 1
            }
        }
    }
    return units
}

pn kind_of(directives) {
    for (directive in directives) {
        if (directive == "type" or directive == "expr") { return directive }
    }
    return null
}

pn expected_error(directives) {
    for (directive in directives) {
        if (starts_with(directive, "error=")) { return slice(directive, 6) }
    }
    return null
}

pn wrapped(directives, code: string) {
    let kind = kind_of(directives)
    if (kind == "type") { return "type x = " ++ code }
    if (kind == "expr") { return "(" ++ code ++ ")" }
    return code
}

pn block_name(ordinal: int) {
    var value = string(ordinal)
    while (len(value) < 4) { value = "0" ++ value }
    return "b" ++ value ++ ".ls"
}

pub pn extract_docs(out_dir: string, index_path: string) any^ {
    io.mkdir(out_dir)^
    var paths = []
    for (path in \.doc.**) {
        if (path.is_file and ends_with(path.name, ".md")) {
            paths = paths ++ [relative_path(path)]
        }
    }
    paths = sort(paths)
    var index = []
    var written = 0
    var skipped = 0
    for (path in paths) {
        for (unit in scan_doc(path)^) {
            let directives = unit.directives
            var entry = {file: path, line: unit.line, directives: directives,
                         kind: kind_of(directives), expect_error: expected_error(directives)}
            if (contains(directives, "no-run")) {
                entry.path = null
                skipped = skipped + 1
            } else {
                let destination = out_dir ++ "/" ++ block_name(len(index))
                let result = output(wrapped(directives, unit.code) ++ "\n", destination, "text")^
                entry.path = destination
                written = written + 1
            }
            index = index ++ [entry]
        }
    }
    let result = output(format(index, {type: "json", indent: 1}), index_path, "text")^
    return {written: written, skipped: skipped, indexed: len(index)}
}

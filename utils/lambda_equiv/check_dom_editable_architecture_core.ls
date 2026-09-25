// Native source ownership checks for check_dom_editable_architecture.py.
import .path_utils

let retired_edit_symbols = ["SYSPROC_SET_SELECTION", "pn_set_selection",
                            "lambda_radiant_set_selection", "dispatch_set_selection"]

fn edit_word_char(ch: string) bool =>
    len(ch) == 1 and contains("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_", ch)
fn edit_space(ch: string) bool => len(ch) == 1 and contains(" \t\r\n", ch)
fn edit_source(name: string) bool =>
    ends_with(name, ".c") or ends_with(name, ".cpp") or
    ends_with(name, ".h") or ends_with(name, ".hpp") or ends_with(name, ".ls")

fn bounded_word(source: string, at: int, word: string) bool =>
    (at == 0 or not edit_word_char(slice(source, at - 1, at))) and
    not edit_word_char(slice(source, at + len(word), at + len(word) + 1))

pub pn has_edit_word(source: string, word: string) {
    var from = 0
    while (from < len(source)) {
        let hit = index_of(slice(source, from), word)
        if (hit == null) { break }
        let at = from + hit
        if (bounded_word(source, at, word)) { return true }
        from = at + len(word)
    }
    return false
}

// Match a two-word declaration while permitting the Python regex's whitespace.
pub pn has_edit_declaration(source: string, keyword: string, name: string,
                            equals: bool) {
    var from = 0
    while (from < len(source)) {
        let hit = index_of(slice(source, from), keyword)
        if (hit == null) { break }
        let at = from + hit
        var cursor = at + len(keyword)
        if (bounded_word(source, at, keyword) and edit_space(slice(source, cursor, cursor + 1))) {
            while (edit_space(slice(source, cursor, cursor + 1))) { cursor = cursor + 1 }
            if (starts_with(slice(source, cursor), name) and bounded_word(source, cursor, name)) {
                cursor = cursor + len(name)
                if (not equals) { return true }
                while (edit_space(slice(source, cursor, cursor + 1))) { cursor = cursor + 1 }
                if (slice(source, cursor, cursor + 1) == "=") { return true }
            }
        }
        from = at + len(keyword)
    }
    return false
}

pub pn edit_source_paths() {
    var paths = []
    for (path in \.lambda.**) {
        if (path.is_file and edit_source(path.name) and not has_link_ancestor(path)) {
            paths = paths ++ [path]
        }
    }
    for (path in \.radiant.**) {
        if (path.is_file and edit_source(path.name) and not has_link_ancestor(path)) {
            paths = paths ++ [path]
        }
    }
    return paths
}

pub pn edit_architecture_failures() any^ {
    // Finish traversal before reading content; nested scans can truncate glob iteration.
    let paths = edit_source_paths()
    var failures = []
    var owners = []
    for (path in paths) {
        let source = input(path, "text")^
        let relative = relative_path(path)
        for (name in retired_edit_symbols) {
            if (has_edit_word(source, name)) {
                failures = failures ++ [relative ++ ": retired editable ABI symbol " ++ name]
            }
        }
        if (contains(source, "base_registry") and
            has_edit_declaration(source, "let", "base_registry", true)) {
            owners = owners ++ [relative]
        }
        if (starts_with(relative, "lambda/editor/") and
            (has_edit_declaration(source, "fn", "extension_key", false) or
             has_edit_declaration(source, "fn", "standard_edit_registry", false))) {
            failures = failures ++ [relative ++ ": duplicate editor-side standard registry"]
        }
    }
    if (owners != ["lambda/dom/edit_registry.ls"]) {
        let found = if (len(owners) == 0) "none" else join(owners, ", ")
        failures = failures ++ ["standard editable registry must be owned only by " ++
                                "lambda/dom/edit_registry.ls; found " ++ found]
    }
    return failures
}

// Native Lambda port of utils/check_js_node_test_separation.py.
import .string_search
fn is_fixture(name: string) bool =>
    starts_with(name, "lib_") or ends_with(name, ".min.js") or
    contains(name, "_src") or contains([
        "dom_jquery_lib.js", "hljs_highlight.js", "underscore_lib.js"
    ], name)

fn is_browser_driver(name: string) bool => contains([
    "dom2_library_probe.js", "dom_bootstrap.js", "dom_jquery_fx.js"
], name)

fn is_node_specifier(name: string) bool => contains([
    "assert", "buffer", "child_process", "cluster", "console", "constants",
    "diagnostics_channel", "domain", "events", "fs", "module", "net", "os",
    "path", "perf_hooks", "process", "punycode", "querystring", "readline",
    "repl", "stream", "string_decoder", "timers", "tty", "url", "util",
    "v8", "vm", "worker_threads", "zlib"
], name)

fn is_space(ch: string) bool =>
    contains(" \t\r\n", ch) or ch == chr(12) or ch == chr(11)
fn is_word(ch: string) bool =>
    len(ch) == 1 and contains("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_", ch)

pn skip_space(source: string, start: int) {
    var pos = start
    while (pos < len(source) and is_space(slice(source, pos, pos + 1))) {
        pos = pos + 1
    }
    return pos
}

// The Python gate accepts `from` or `require(` followed by whitespace and a quoted bare specifier.
pn has_import_after(source: string, marker: string) {
    for (position in text_positions(source, marker)) {
        let quote_pos = skip_space(source, position + len(marker))
        let quote = slice(source, quote_pos, quote_pos + 1)
        if (quote == "'" or quote == "\"") {
            var start = quote_pos + 1
            if (starts_with(slice(source, start), "node:")) { start = start + 5 }
            var stop = start
            while (stop < len(source) and slice(source, stop, stop + 1) != quote) {
                stop = stop + 1
            }
            if (stop < len(source) and is_node_specifier(slice(source, start, stop))) {
                return true
            }
        }
    }
    return false
}

pn has_node_global(source: string) {
    for (token in ["Buffer.", "process.nextTick", "__dirname", "__filename", "module.exports"]) {
        for (position in text_positions(source, token)) {
            if (position == 0 or not is_word(slice(source, position - 1, position))) {
                return true
            }
        }
    }
    return false
}

pn main() {
    var failures = 0
    for (path in \.test.js.**) {
        let name = path.name
        if (path.is_file and ends_with(name, ".js") and
            not is_fixture(name) and not is_browser_driver(name)) {
            let rel = string(path)
            let source = input(path, "text")^
            if (starts_with(name, "jube_")) {
                print(rel ++ ": Jube tests exercise Node modules and belong in test/node\n")
                failures = failures + 1
            }
            if (has_import_after(source, "from") or has_import_after(source, "require(") or
                has_node_global(source)) {
                print(rel ++ ": Node API coverage belongs in test/node\n")
                failures = failures + 1
            }
        }
    }
    if (failures > 0) { raise error("JS_NODE_TEST_SEPARATION: failed with " ++ string(failures) ++ " violation(s)") }
    print("JS_NODE_TEST_SEPARATION: passed\n")
}

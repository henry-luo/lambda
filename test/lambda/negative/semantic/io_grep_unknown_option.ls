// @expect-error: E312
// @description: S17.8.1 — an option name io.grep does not define is a
// compile-time error when the options are a map literal at the call (a map
// that arrives as a value only draws a run-time warning).

pn main() {
    io.grep("test/input/grep_tree", "TODO", {line: true, linez: true})
}

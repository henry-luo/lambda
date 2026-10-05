// @expect-error: E312
// @description: S17.8.1 — an option name io.search does not define is a
// compile-time error when the options are a map literal at the call; whole_line
// is io.grep's, which io.search does not take (FTX12).

pn main() {
    io.search("test/input/fts_tree", "search", {line: true, whole_line: true})
}

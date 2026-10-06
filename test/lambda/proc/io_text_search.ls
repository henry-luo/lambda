// io.text_search(source, query, options?) — ranked full-text search over files with
// lib/fts (vibe/Lambda_IO_Fulltext_Search.md, FTX11). Fixture: test/input/fts_tree,
// whose .ignore skips *.skip. Scores are printed only through the order they give.

pn files_of(hits) {
    [for (h in hits) h.file]
}

pn main() {
    // ranked files, best first; every result names its file (FTX7, FTX11)
    let ranked = io.text_search(\.test.input.fts_tree, "search")^
    print("T1:", files_of(ranked), " ", ranked[0].score > ranked[1].score)
    print("\n")

    // paragraphs are documents; text is on by default, line on request (FTX2, FTX12)
    print("T2:", io.text_search(\.test.input.fts_tree.'guide.md', "\"inverted index\"", {unit: "paragraph", line: true})^)
    print("\n")

    // part-of-token terms: start, end, any part (FTX13)
    print("T3:", files_of(io.text_search(\.test.input.fts_tree, "pars*", {rank: false})^))
    print("\n")
    print("T4:", files_of(io.text_search(\.test.input.fts_tree, "*port", {rank: false})^))
    print("\n")

    // word: false makes a bare term match any part of a token, as io.grep matches
    print("T5:", files_of(io.text_search(\.test.input.fts_tree, "port", {rank: false})^), " ",
        files_of(io.text_search(\.test.input.fts_tree, "port", {word: false, rank: false})^))
    print("\n")

    // or, not and grouping; a stray parenthesis is no error (FTX6)
    print("T6:", files_of(io.text_search(\.test.input.fts_tree, "(bm25 or relevance) -index", {rank: false})^))
    print("\n")
    print("T7:", files_of(io.text_search(\.test.input.fts_tree, "search ) \"unclosed", {rank: false})^))
    print("\n")

    // lines: the "\r" of "\r\n" is no content; line_ending and context on request
    print("T8:", io.text_search(\.test.input.fts_tree.'crlf.txt', "crlf", {unit: "line", rank: false, line: true, line_ending: true, context: 1})^)
    print("\n")

    // matches: the token occurrences, index in code points from the file's start
    print("T9:", io.text_search(\.test.input.fts_tree.'unicode.txt', "caf* lait", {unit: "line", rank: false, matches: true, byte_offset: true})^)
    print("\n")

    // a snippet around the most query terms, cut with an ellipsis
    print("T10:", io.text_search(\.test.input.fts_tree.'guide.md', "index build", {rank: false, snippet: 4})^[0].snippet)
    print("\n")

    // files and count answer about files, in path order (GRP21, GRP30)
    print("T11:", io.text_search(\.test.input.fts_tree, "search", {files: true})^)
    print("\n")
    print("T12:", io.text_search(\.test.input.fts_tree, "search", {unit: "line", count: true})^)
    print("\n")

    // limit keeps the best; limit_per_file the best of each file (FTX12)
    print("T13:", len(io.text_search(\.test.input.fts_tree, "search", {unit: "line", limit: 2})^), " ",
        len(io.text_search(\.test.input.fts_tree, "search", {unit: "line", limit_per_file: 1})^))
    print("\n")

    // ignore_case is on by default for full-text search (FTX4)
    print("T14:", len(io.text_search(\.test.input.fts_tree, "SEARCH")^), " ",
        len(io.text_search(\.test.input.fts_tree, "SEARCH", {ignore_case: false})^))
    print("\n")

    // unaccent: cafe finds café (FTX4)
    print("T15:", len(io.text_search(\.test.input.fts_tree, "cafe")^), " ",
        len(io.text_search(\.test.input.fts_tree, "cafe", {unaccent: true})^))
    print("\n")

    // CJK and Hangul are a token per character, so phrases find words (FTX3)
    print("T16:", files_of(io.text_search(\.test.input.fts_tree, "检索 학교", {rank: false})^))
    print("\n")

    // a stop word still takes its place in a phrase: "ranks the documents" needs a
    // word between ranks and documents, which the fixture does not have
    print("T17:", files_of(io.text_search(\.test.input.fts_tree, "\"ranks documents by relevance\"", {rank: false})^), " ",
        files_of(io.text_search(\.test.input.fts_tree, "\"ranks the documents by relevance\"", {rank: false, stopwords: ["the"]})^))
    print("\n")

    // hidden entries and ignore files, as io.grep (GRP14)
    print("T18:", len(io.text_search(\.test.input.fts_tree, "search", {files: true, hidden: true})^), " ",
        len(io.text_search(\.test.input.fts_tree, "search", {files: true, ignore: false})^))
    print("\n")

    // tf ranking and several sources
    print("T19:", files_of(io.text_search([\.test.input.fts_tree.'ranking.txt', \.test.input.fts_tree.code], "parser or ranks", {rank: "tf"})^))
    print("\n")

    // errors: a missing source, an unknown unit, files with count
    var missing: any | error = io.text_search("test/input/fts_tree/nope", "x")
    var bad_unit: any | error = io.text_search(\.test.input.fts_tree, "x", {unit: "chapter"})
    var both: any | error = io.text_search(\.test.input.fts_tree, "x", {files: true, count: true})
    print("T20:", missing is error, " ", bad_unit is error, " ", both is error)
    print("\n")
}

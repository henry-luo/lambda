// io.grep(source, pattern, options?) — line-oriented search over files with
// lib/grep (vibe/Lambda_Lib_Grep.md §9B, GRP26). Fixture: test/input/grep_tree,
// whose .ignore skips *.skip and ignored_dir/ (a .ignore, not a .gitignore, so
// git keeps the skipped fixtures).

pn main() {
    // one file: {value, index}, index in code points like in-memory find
    print("T1:", io.grep("test/input/grep_tree/notes.txt", "TODO")^)
    print("\n")

    // a directory honours ignore files and skips hidden entries and known
    // dependency directories (GRP14); files come in path order
    print("T2:", io.grep(\.test.input.grep_tree, "TODO", {line: true})^)
    print("\n")

    // a string pattern; text is the whole line, without a "\r\n" terminator
    print("T3:", io.grep(\.test.input.grep_tree.'crlf.txt', \("TODO:" "\s"* "\w"+), {text: true})^)
    print("\n")

    // ignore_case folds as in-memory find does (S17.7.1)
    print("T4:", io.grep(\.test.input.grep_tree.code, "todo", {ignore_case: true, line: true})^)
    print("\n")

    // files: the paths of files with a match
    print("T5:", io.grep(\.test.input.grep_tree, "TODO", {files: true})^)
    print("\n")

    // include and exclude globs (GRP22)
    print("T6:", io.grep(\.test.input.grep_tree, "TODO", {files: true, include: "*.ls"})^)
    print("\n")
    print("T7:", io.grep(\.test.input.grep_tree, "TODO", {files: true, exclude: ["code", "*.txt"]})^)
    print("\n")

    // every layer off: hidden entries, ignored files and dependency directories
    print("T8:", len(io.grep(\.test.input.grep_tree, "TODO", {hidden: true, ignore: false})^))
    print("\n")

    // invert reports the lines without a match (GRP24)
    print("T9:", io.grep("test/input/grep_tree/notes.txt", "TODO", {invert: true, line: true})^)
    print("\n")

    // context: the neighbouring lines of each match
    print("T10:", io.grep("test/input/grep_tree/notes.txt", "beta", {context: 1})^)
    print("\n")

    // a total limit keeps the first matches in path order; a per-file limit
    // caps each file (GRP25)
    print("T11:", io.grep(\.test.input.grep_tree, "TODO", {limit: 3, line: true})^)
    print("\n")
    print("T12:", len(io.grep(\.test.input.grep_tree, "TODO", {limit_per_file: 1})^))
    print("\n")

    // whole_line matches an entire line; the "\r" of "\r\n" is not content
    print("T13:", io.grep("test/input/grep_tree/crlf.txt", "end", {whole_line: true, line: true})^)
    print("\n")

    // index counts code points, byte_offset bytes
    print("T14:", io.grep("test/input/grep_tree/unicode.txt", "TODO", {byte_offset: true})^)
    print("\n")

    // a trailing * searches a directory's own files, ** everything below
    print("T15:", io.grep(\.test.input.grep_tree.*, "TODO", {files: true})^)
    print("\n")
    print("T16:", io.grep(\.test.input.grep_tree.code.**, "x", {files: true})^)
    print("\n")

    // several sources and several patterns; word matches whole words only
    print("T17:", io.grep(["test/input/grep_tree/notes.txt", "test/input/grep_tree/code"],
        ["beta", \("\d"+)], {line: true})^)
    print("\n")
    print("T18:", io.grep("test/input/grep_tree/code/main.ls", "x", {word: true})^)
    print("\n")

    // a missing source is an error
    var missing: any | error = io.grep("test/input/grep_tree/nope.txt", "x")
    print("T19:", missing is error)
    print("\n")

    // S17.8.1: an unknown name in an options value is ignored with a warning
    let opts = {line: true, colour: "never"}
    print("T20:", io.grep("test/input/grep_tree/notes.txt", "beta", opts)^)
    print("\n")

    // count: the lines with a match per file, files without one left out (GRP30)
    print("T21:", io.grep(\.test.input.grep_tree, "TODO", {count: true})^)
    print("\n")

    // a count names its file even for one source; invert counts the other lines
    print("T22:", io.grep("test/input/grep_tree/notes.txt", "TODO", {count: true, invert: true})^)
    print("\n")

    // the limits count lines: limit_per_file caps each count, limit their sum
    print("T23:", io.grep(\.test.input.grep_tree, "TODO", {count: true, limit_per_file: 1})^)
    print("\n")
    print("T24:", io.grep(\.test.input.grep_tree, "TODO", {count: true, limit: 3})^)
    print("\n")

    // line_ending: "\n", "\r\n", or null for a last line without one (GRP31)
    print("T25:", io.grep("test/input/grep_tree/eol.txt", "line", {line: true, line_ending: true})^)
    print("\n")

    // an empty pattern matches every line, so a count is the number of lines;
    // line_ending has nothing to apply to in a count
    print("T26:", io.grep("test/input/grep_tree/eol.txt", "", {count: true, line_ending: true})^)
    print("\n")

    // files and count both say what a result is: asking for both is an error
    var both: any | error = io.grep("test/input/grep_tree/notes.txt", "TODO", {files: true, count: true})
    print("T27:", both is error)
    print("\n")
}

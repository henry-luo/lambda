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
    print("T3:", io.grep(\.test.input.grep_tree.'crlf.txt', \("TODO:" s* w+), {text: true})^)
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
        ["beta", \(d+)], {line: true})^)
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
}

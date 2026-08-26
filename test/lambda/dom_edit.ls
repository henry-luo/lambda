// F5 word-boundary scanners from the dom package's editing applier.
import editing: lambda.package.dom.editing

let back = {
    mid_word: editing.word_start("hello world", 11),
    after_spaces: editing.word_start("abc   ", 6),
    at_zero: editing.word_start("abc", 0),
    across_punct: editing.word_start("a.b", 3)
}

let fwd = {
    from_zero: editing.word_end("hello world", 0),
    skip_leading: editing.word_end("  abc", 0),
    at_end: editing.word_end("abc", 3),
    astral: editing.word_end("a😀b", 0)
}

let sanitize = {
    single_lf: editing.sanitize("a\nb", false),
    single_crlf: editing.sanitize("a\r\nb", false),
    single_cr: editing.sanitize("a\rb", false),
    multi_kept: editing.sanitize("a\nb", true),
    multi_crlf: for (i in 0 to len(editing.sanitize("a\r\nb", true)) - 1)
        ord(slice(editing.sanitize("a\r\nb", true), i, i + 1)),
    multi_cr: for (i in 0 to len(editing.sanitize("a\rb", true)) - 1)
        ord(slice(editing.sanitize("a\rb", true), i, i + 1))
}

{ back: back, fwd: fwd, sanitize: sanitize }

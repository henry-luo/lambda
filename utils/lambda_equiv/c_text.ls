// Remove C comments with the first opening marker taking precedence.
pub pn strip_c_comments(source: string) {
    var out = ""
    var pos = 0
    while (pos < len(source)) {
        let tail = slice(source, pos)
        let block = index_of(tail, "/*")
        let line = index_of(tail, "//")
        if (block == null and line == null) {
            out = out ++ tail
            break
        }
        let use_block = block != null and (line == null or block <= line)
        let start = if (use_block) block else line
        out = out ++ slice(tail, 0, start)
        if (use_block) {
            let close = index_of(slice(tail, start + 2), "*/")
            if (close == null) {
                // An unterminated opener is ordinary text to Python's regex.
                out = out ++ slice(tail, start)
                break
            }
            pos = pos + start + 2 + close + 2
        } else {
            let newline = index_of(slice(tail, start + 2), "\n")
            if (newline == null) { break }
            pos = pos + start + 2 + newline
        }
    }
    return out
}

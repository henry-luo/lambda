// Executable arguments remain separate from the input stream.
pub pn cmd(input_or_program, program_or_args, args = null, stderr = null, timeout_ms = null) any^ {
    if (args == null) {
        io.shell_start(null, input_or_program, program_or_args, stderr, timeout_ms)
    } else {
        io.shell_start(input_or_program, program_or_args, args, stderr, timeout_ms)
    }
}

pub pn stdin() any^ { io.shell_stdin() }

pub pn git(...) any^ { io.shell_start(null, "git", varg(), null, null) }

pub pn ls(stderr = null, timeout_ms = null) any^ { io.shell_start(null, "ls", [], stderr, timeout_ms) }

pub pn cat(input = null, stderr = null, timeout_ms = null) any^ {
    io.shell_start(input, "cat", [], stderr, timeout_ms)
}

pub pn sort(input = null, stderr = null, timeout_ms = null) any^ {
    io.shell_start(input, "sort", [], stderr, timeout_ms)
}

pub pn uniq(input = null, count: bool = false, stderr = null, timeout_ms = null) any^ {
    io.shell_start(input, "uniq", if (count) ["-c"] else [], stderr, timeout_ms)
}

pub pn head(input = null, lines: int = 10, stderr = null, timeout_ms = null) any^ {
    io.shell_start(input, "head", ["-n", string(lines)], stderr, timeout_ms)
}

// Whole-value pipes pass their source as the first argument (S10.1.2v7).
// Text cannot be shell input; binary can be input or an argv byte string.
// Treat binary as input and use cmd for a binary first argument.
pn direct(source, program, arguments) any^ {
    if (source is string or source is symbol) {
        io.shell_start(null, program, [source, *arguments], null, null)
    } else {
        io.shell_start(source, program, arguments, null, null)
    }
}
pub pn grep(source = null, ...) any^ { direct(source, "grep", varg()) }
pub pn cut(source = null, ...) any^ { direct(source, "cut", varg()) }
pub pn tr(source = null, ...) any^ { direct(source, "tr", varg()) }
pub pn sed(source = null, ...) any^ { direct(source, "sed", varg()) }
pub pn awk(source = null, ...) any^ { direct(source, "awk", varg()) }
pub pn tee(source = null, ...) any^ { direct(source, "tee", varg()) }
pub pn wc(source = null, ...) any^ { direct(source, "wc", varg()) }
pub pn tail(source = null, ...) any^ { direct(source, "tail", varg()) }
pub pn paste(source = null, ...) any^ { direct(source, "paste", varg()) }
pub pn xargs(source = null, ...) any^ { direct(source, "xargs", varg()) }
pub pn printf(...) any^ { direct(null, "printf", varg()) }
pub pn find(source = null, ...) any^ { direct(source, "find", varg()) }
pub pn yes(...) any^ { direct(null, "yes", varg()) }
pub pn sleep(...) any^ { direct(null, "sleep", varg()) }

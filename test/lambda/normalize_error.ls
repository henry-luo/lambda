// normalize must propagate an error in its optional mode argument.

fn fail_mode() string^ {
    raise error("normalize mode error")
}

fn normalize_result(mode: string | error) {
    let value = normalize("text", mode) ^ { ^ }
    if (value is error) "error" else value
}

fn test_normalize_error() {
    let mode = fail_mode() ^ { ^ }
    normalize_result(mode)
}

test_normalize_error()
normalize_result(error("normalize mode error"))

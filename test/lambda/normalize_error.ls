// normalize must propagate an error in its optional mode argument.

fn fail_mode() string^ {
    raise error("normalize mode error")
}

fn normalize_result(mode: string | error) {
    let value^err = normalize("text", mode)
    if (^err) "error" else value
}

fn test_normalize_error() {
    let mode^err = fail_mode()
    normalize_result(err)
}

test_normalize_error()
normalize_result(error("normalize mode error"))

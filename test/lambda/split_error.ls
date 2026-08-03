// split must propagate an error in any argument.

fn fail_string() string^ {
    raise error("split argument error")
}

fn split_result(source, separator, keep) {
    let result^err = split(source, separator, keep)
    if (^err) "error" else "ok"
}

fn test_split_errors() {
    let source^source_err = fail_string()
    let separator^separator_err = fail_string()
    let keep^keep_err = fail_string()
    [
        split_result(source_err, ",", false),
        split_result("a,b", separator_err, false),
        split_result("a,b", ",", keep_err)
    ]
}

test_split_errors()

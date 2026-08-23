// split must propagate an error in any argument.

fn fail_string() string^ {
    raise error("split argument error")
}

fn split_result(source, separator, keep) {
    let result = split(source, separator, keep) ^ { ^ }
    if (result is error) "error" else "ok"
}

fn test_split_errors() {
    let source = fail_string() ^ { null }
    let separator = fail_string() ^ { null }
    let keep = fail_string() ^ { ^ };
    [
        split_result(source_err, ",", false),
        split_result("a,b", separator_err, false),
        split_result("a,b", ",", keep)
    ]
}

test_split_errors()

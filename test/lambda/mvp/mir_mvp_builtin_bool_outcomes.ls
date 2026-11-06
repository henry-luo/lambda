// S7.8.1/S7.9.3: C Bool's error sentinel remains an ordinary Error outcome.
fn contains_open(value: any, part: any) any { contains(value, part) }
fn starts_open(value: any, part: any) any { starts_with(value, part) }
fn ends_open(value: any, part: any) any { ends_with(value, part) }
fn propagated(value: any) any^ {
    let checked = starts_with(value, "x")^
    return 99
}

pn main() {
    print([contains_open(7, "x") is error, starts_open(7, "x") is error,
        ends_open(7, "x") is error]); print("\n")
    print([(not contains_open(7, "x")) is error,
        (if (starts_open(7, "x")) 1 else 2) is error]); print("\n")
    let failure = shl(1, -1)
    print([contains_open(failure, "x") is error, starts_open("x", failure) is error,
        ends_open(failure, "x") is error]); print("\n")
    print([contains_open("abc", "b"), starts_open("abc", "a"), ends_open("abc", "c")]); print("\n")
    print([contains_open("abc", "z"), starts_open(null, "a"), ends_open("abc", null)]); print("\n")
    print([propagated(7) is error, propagated("x") == 99]); print("\n")
}

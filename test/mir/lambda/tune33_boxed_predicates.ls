// S3.4/S6.1.2: boxed comparison predicates preserve null and error behavior.
pn classify(left: any | error, right: any | error) {
    var flags = 0
    if (left == right) { flags = flags + 1 }
    if (left != right) { flags = flags + 2 }
    if (left < right) { flags = flags + 4 }
    if (left <= right) { flags = flags + 8 }
    if (left > right) { flags = flags + 16 }
    if (left >= right) { flags = flags + 32 }
    return flags
}
pn logical(left: any | error, right: any | error) {
    return [left == right and 7, left != right or "fallback"]
}
fn error_branch(left: any | error, right: any | error) int | error => if (left < right) 1 else 0
fn compound(left: any | error, right: any | error) int | error =>
    if ((left < right and left != null) or left == right) 1 else 0
fn mixed_result(value) => if (value == 1 and 7) 1 else 0
fn vector_predicate() int | error => if ([2, 3] lt [0, 0]) 1 else 0
pn observe(var calls, value) {
    calls = calls + 1
    return value
}
pn single_evaluation() {
    var calls = 0
    var value = 0
    if (observe(calls, 1) == observe(calls, 1)) { value = 1 }
    return [calls, value]
}
pn main() {
    print([classify(1, 1), classify(1, 2), classify(2, 1),
        classify(null, 1), classify(null, null), classify("a", "b")]); print("\n")
    print([logical(1, 1), logical(1, 2), logical(null, null)]); print("\n")
    let failure = error("predicate")
    let errors = logical(failure, 1)
    // discarded pn conditions skip their branches; expression results retain errors.
    print([classify(failure, 1) == 0, classify(1, failure) == 0,
        type(error_branch(failure, 1)) == error,
        type(error_branch(1, failure)) == error,
        type(errors[0]) == error, type(errors[1]) == error]); print("\n")
    print(single_evaluation()); print("\n")
    print([compound(1, 2), compound(2, 1), compound(1, 1),
        type(compound(failure, 1)) == error,
        mixed_result(1), mixed_result(2), vector_predicate()]); print("\n")
}

// S7.7.4v2/SI14v3: borrowed whole bindings publish outcomes without lane loss.
pn replace_array_later(var values: int[]) {
    values = fill(3, 9)^
    values[1] = 7
}
pn replace_array_outcome(var values: int[], next) { values = next }
pn forward_array_outcome(var values: int[], next) {
    let outcome = replace_array_outcome(values, next)
}
pn main() {
    var values: int[] = [1]
    let snapshot = values
    replace_array_later(values)^
    var value: int = 1
    let before = () int => value
    replace_later(value, "bad")
    let after = () any^ => value
    let observed = after()
    var unchanged: int = 7
    let early = propagate_before_store(unchanged);
    var failed_array: int[] = [3, 4]
    let kept_array = failed_array
    let outcome = forward_array_outcome(failed_array, "bad")
    // S7.9.3: indexing a retained error preserves it; S7.1.1v3 covers non-error bases.
    let array_failed = failed_array is error and failed_array[0] is error
    failed_array = [8];
    [before(), observed is error, value is error, unchanged, early.message,
        values, snapshot, array_failed, failed_array, kept_array]
}
pn replace_later(var value: int, next) { value = next }
fn failed() int^ => error("before store")
pn propagate_before_store(var value: int) int^ {
    value = failed()^
    return 0
}

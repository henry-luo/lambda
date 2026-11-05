// D4.3.1/D4.3.2: private lanes retain the erased contract and every caller domain.
fn count_down(n) {
    if (n < 1) { return n }
    return count_down(n - 1)
}
fn subtract(x, y) {
    var remaining = x
    while (remaining >= y) { remaining = remaining - y }
    return remaining
}
fn identity(value) { value }
fn relay(value) { identity(value) }
fn returned(value) { value }
fn callback_result(value) { value }
pn borrowed(var value) { value = "changed" }
pn observes_borrow(value) { borrowed(value); return value }
fn inspect(value) { value is int }
fn recursive_sum(n) {
    if (n < 1) { return 0 }
    return n + recursive_sum(n - 1)
}
fn leave_null(n) {
    if (n == 0) { return null }
    return n
}
fn collected(n) { n; n + 1 }
fn result_error(fail) {
    if (fail) { return shl(1, -1) }
    return 3
}
fn recursive_float(n) {
    if (n < 1.0) { return 0.0 }
    return n + recursive_float(n - 1.0)
}
fn possible_record(flag) {
    if (flag) { return 4 }
    return {value: 1}
}
fn possible_array(flag) {
    if (flag) { return 4 }
    return [1, 2]
}
fn explicit_sum(n) any^ {
    if (n < 1) { return 0 }
    return n + explicit_sum(n - 1)
}
fn explicit_float(n) any^ {
    if (n < 1.0) { return 0.0 }
    return n + explicit_float(n - 1.0)
}
fn nullable_top(n) any {
    if (n == 0) { return null }
    return n
}
fn mixed_top(flag) any {
    if (flag) { return 1 }
    return 2.5
}
fn explicit_error(fail) any^ {
    if (fail) { return shl(1, -1) }
    return 7
}

pn main() {
    print([count_down(20), subtract(101, 7)]); print("\n")
    print([relay(3), relay(2.5), relay(null), relay("text"), relay(true)]); print("\n")
    let failure = shl(1, -1)
    print(relay(failure) is error); print("\n")
    var changing = 1
    print(returned(changing)); print("\n")
    changing = "later"
    print(returned(changing)); print("\n")
    let callback = (value) => callback_result(value)
    print(callback(2.5)); print("\n")
    print(observes_borrow(7)); print("\n")
    print([inspect(4), inspect(1.5)]); print("\n")
    print([recursive_sum(10), leave_null(0), leave_null(2), len(collected(3))]); print("\n")
    print([result_error(false), result_error(true) is error]); print("\n")
    print(recursive_float(10.0)); print("\n")
    print([type(relay(3)) == int, type(relay(2.5)) == float]); print("\n")
    var record = possible_record(false)
    var values = possible_array(false)
    record.value = 9
    values[0] = 8
    print([record.value, values[0]]); print("\n")
    print([explicit_sum(10) == 55, explicit_float(10.0) == 55.0,
        nullable_top(0) is null, nullable_top(2) == 2,
        type(mixed_top(true)) == int, type(mixed_top(false)) == float,
        explicit_error(true) is error, explicit_error(false) == 7]); print("\n")
}

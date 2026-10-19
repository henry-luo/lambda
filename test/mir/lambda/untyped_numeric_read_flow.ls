// S7.1.1v3, D3.3.3v3: a producer hint never replaces the live array guard.
pn probability_source(mode, var values) {
    values[0] = 0.25
    values[1] = 0.75
    values[2] = 0.0 / 0.0
    if (mode == 1) { values = [1, 2] }
    if (mode == 2) { values = ["a", "z"] }
    if (mode == 3) { values = [] }
    if (mode == 4) { values = [null, 0.5] }
    if (mode == 5) { values = [1.0 / 0.0, -1.0 / 0.0] }
    if (mode == 6) { values = "ab" }
    return values
}
pn probability_compare(mode, index: int, threshold: any | error) {
    var values = fill(3, 0.0)
    probability_source(mode, values)
    var other = threshold
    return [values[index] < other, values[index] <= other,
        values[index] > other, values[index] >= other]
}
pn probability_materialized(mode, index: int, threshold: any | error) {
    var values = fill(3, 0.0)
    probability_source(mode, values)
    var value = values[index]
    return [value < threshold, value <= threshold, value > threshold, value >= threshold]
}
pn length_rebound(mode) {
    var count = len([1, 2])
    if (mode == 1) { count = 8 }
    return count - 1
}
pn borrowed_length(var count) { count = 9 }
pn length_borrowed() {
    var count = len([1, 2])
    borrowed_length(count)
    return count - 1
}
// separate call shapes exercise each nullable payload and its scalar return ABI.
pn nullable_int_order(flag, values, index) {
    let left = values[index]
    let right = values[index + 1]
    var value = if (flag) left else right
    return [value, value < 0, value >= 0, value == null]
}
pn nullable_float_local(flag, values, index) {
    var value = if (flag) values[index] else values[index + 1]
    return [value, value + 1.0]
}
pn nullable_bool_local(flag, values, index) {
    var value = if (flag) values[index] else values[index + 1]
    return [value, value and true, not value]
}
pn nullable_float_result(flag, values, index) {
    return if (flag) values[index] else values[index + 1]
}
pn nullable_bool_result(flag, values, index) {
    return if (flag) values[index] else values[index + 1]
}
// a later same-kind read can still introduce absence into a recovered lane.
pn nullable_float_write(flag, left, right, values, index) {
    var value = if (flag) left else right
    value = values[index]
    return [value, value + 1.0]
}
pn inferred_read(values, index) { return values[index] }
pn nullable_opaque_result(flag, values, index) {
    return if (flag) inferred_read(values, index) else inferred_read(values, index + 1)
}
pn main() {
    print(probability_compare(0, 0, 0.5)); print("\n")
    print(probability_compare(0, 1, 0.5)); print("\n")
    print(probability_compare(0, 2, 0.5)); print("\n")
    print(probability_compare(0, -1, 0.5)); print("\n")
    print(probability_compare(1, 0, 1.0)); print("\n")
    print(probability_compare(3, 0, 0.5)); print("\n")
    print(probability_compare(4, 0, 0.5)); print("\n")
    print(probability_compare(5, 0, 0.5)); print("\n")
    print(probability_compare(6, 0, "b")); print("\n")
    var mode = 0
    var equal = true
    while (mode < 7) {
        // compare the real indexed expression with a separately materialized read.
        if (mode != 2) {
            let threshold = if (mode == 6) "b" else 0.5
            equal = equal and probability_compare(mode, 0, threshold) == probability_materialized(mode, 0, threshold)
        }
        mode = mode + 1
    }
    let errors = probability_compare(2, 0, error("comparison operand"))
    print([equal, type(errors[0]) == error, type(errors[3]) == error]); print("\n")
    print([length_rebound(0), length_rebound(1), length_borrowed()]); print("\n")
    print([nullable_int_order(true, [2], 0), nullable_int_order(true, [2], 3),
        nullable_int_order(false, [2], 0)]); print("\n")
    print([nullable_float_local(true, [0.5], 0), nullable_float_local(true, [0.5], 3),
        nullable_float_local(false, [0.5], 0)]); print("\n")
    print([nullable_bool_local(true, [true], 0), nullable_bool_local(true, [true], 3),
        nullable_bool_local(false, [true], 0)]); print("\n")
    print([nullable_float_result(true, [0.5], 0), nullable_float_result(true, [0.5], 3),
        nullable_float_result(false, [0.5], 0)]); print("\n")
    print([nullable_float_write(true, 1.0, 2.0, [0.5], 0),
        nullable_float_write(false, 1.0, 2.0, [0.5], 3)]); print("\n")
    print([nullable_opaque_result(true, [0.5], 0),
        nullable_opaque_result(false, [0.5], 0)]); print("\n")
    print([nullable_bool_result(true, [true], 0), nullable_bool_result(true, [true], 3),
        nullable_bool_result(false, [true], 0)]); print("\n")
}

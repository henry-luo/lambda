// D4.3.1/D7.4.1: a constructor hint selects a guarded body, never admission.
pn count_and_clear(var flags, limit: int) any {
    var count = 0
    for index in 0 to limit - 1 {
        if (flags[index]) { count = count + 1 }
        flags[index] = false
    }
    return count
}
fn lookup(values: any, key: any) any => values[key]
fn text_lookup(values: string, key: any) any => values[key]
fn coefficient(value) => value * (value + 1) * (value + 2) div 6
fn sum_coefficients(values, count: int) any^ {
    var total = 0
    var index = 0
    while (index < count) {
        let member = values[index]
        total = total + coefficient(member)
        index = index + 1
    }
    return total
}
pn main() {
    var flags = fill(6, true)^
    let count = count_and_clear(flags, 6)^
    print([count, flags]); print("\n")
    flags = fill(6, true)^
    let saved = flags
    let shared_count = count_and_clear(flags, 6)^
    print([shared_count, flags, saved]); print("\n")
    // different lanes and shorter owners must enter the original body.
    var numbers: any = fill(4, 1.5)^
    let number_count = count_and_clear(numbers, 4)^
    print([number_count, numbers]); print("\n")
    var words: any = ["a", "b", "c", "d"]
    let word_count = count_and_clear(words, 4)^
    print([word_count, words]); print("\n")
    // S7.1.1v3: erased keys retain exact-value, absence and Error behavior.
    let keys = parse("[1, -1, 4, 1.0, null]", 'json')^
    let integers = fill(4, 10)^
    print([lookup(integers, keys[0]), lookup(fill(4, 1.5)^, keys[0]),
        lookup(fill(4, true)^, keys[0]), lookup(["a", "b"], keys[0]),
        lookup(integers, keys[1]), lookup(integers, keys[2]),
        lookup(integers, keys[3]), lookup(integers, keys[4]),
        lookup(integers, nan), lookup("abc", keys[0])]); print("\n")
    let problem = error({code: 7011, message: "key kept"})
    print(lookup(integers, problem).message); print("\n")
    print([text_lookup("abc", keys[0]), text_lookup("abc", keys[1])]); print("\n")
    // D7.4.1/S4.4: closed integer leaves retain lane and Error fallbacks.
    print([sum_coefficients([1, 2, 3, 4], 4),
        sum_coefficients([1.0, 2.0, 3.0, 4.0], 4) == 35.0,
        sum_coefficients([1, null, 3], 3),
        sum_coefficients([1, "bad", 3], 3) is error,
        sum_coefficients([inf], 1) == inf]); print("\n")
}

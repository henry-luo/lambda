// D4.3.1/S9.1.2: a physical lane is no receiving contract; snapshots detach.
pn bool_store(var values: any, key: int, member: bool) any {
    values[key] = member
    return values
}
pn float_store(var values: any, key: int, member: float) any {
    values[key] = member
    return values
}
pn stride(var values: any, first: int, limit: int, step: int) any^ {
    var index = first
    while (index <= limit) {
        values[index] = false
        index = index + step
    }
    return index
}
pn stride_offset(var values: any, first: int, limit: int, step: int) any^ {
    var index = first
    while (index < limit) {
        values[index + 1] = false
        index = index + step
    }
    return index
}
pn main() {
    var flags = fill(3, true)^
    bool_store(flags, 1, false)^
    let saved_flags = flags
    bool_store(flags, 0, false)^
    print([flags, saved_flags]); print("\n")
    var numbers = fill(3, 1.5)^
    float_store(numbers, 1, 2.5)^
    let saved_numbers = numbers
    float_store(numbers, 0, -2.5)^
    print([numbers, saved_numbers]); print("\n")
    var mixed: any = fill(2, 0)^
    bool_store(mixed, 0, false)^
    float_store(mixed, 1, 3.5)^
    print(mixed); print("\n")
    var text: any = ["one", "two"]
    bool_store(text, 1, true)^
    print(text); print("\n")
    // S7.1.1v3/S9.1.2: an unvisited upper endpoint must preserve the last
    // actual store and counter publication, including shared/fallback owners.
    var strided = fill(6, true)^
    let original = strided
    let end = stride(strided, 0, 5, 2)^
    print([end, strided, original]); print("\n")
    var mixed_stride: any = ["a", "b", "c"]
    let mixed_end = stride(mixed_stride, 0, 2, 2)^
    print([mixed_end, mixed_stride, stride(mixed_stride, 3, 2, 1)]); print("\n")
    // S7.1.1v3: the last visited key fits, while the enclosing key does not.
    var offset_unique = fill(6, true)^
    let unique_end = stride_offset(offset_unique, 0, 6, 2)^
    print([unique_end, offset_unique]); print("\n")
    var offset_values = fill(6, true)^
    let offset_saved = offset_values
    let offset_end = stride_offset(offset_values, 0, 6, 2)^
    print([offset_end, offset_values, offset_saved]); print("\n")
}

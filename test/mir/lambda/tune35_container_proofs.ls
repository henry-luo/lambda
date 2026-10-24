// S11.4.10: broad membership proofs do not admit refined elements or shapes.
type Entry = {value: int}

pn concatenate(left: array, right: array) array {
    let result: array = left ++ right
    return result
}

pn choose(flag: bool, left: array, right: array) array {
    if (flag) { concatenate(left, right) } else { ["fallback"] }
}

pn narrow(values: int[]) int { return values[0] }
pn shaped(value: Entry) int { return value.value }
pn require_array(value) array { var checked: array = value; return checked }

pn extend(var value: map, key: string, child) {
    value[key] = child
    value.fixed = 7
}

pn main() {
    var source: array = [1, "two", null]
    let snapshot = source
    source = concatenate(source, [true])
    print([snapshot, source, choose(false, source, [])]); print("\n")
    var record: map = {value: 1}
    let before = record
    extend(record, "extra", [2])
    print([before, record]); print("\n")
    let numbers: array = [2, 3]
    let refined: map = {value: 9}
    print([narrow(numbers), shaped(refined), type(require_array(3))])
    print("\n")
}

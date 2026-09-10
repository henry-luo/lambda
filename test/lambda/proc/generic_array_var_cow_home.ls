// A generic `array` var borrow must publish its COW replacement to the caller.
// S9.1.2/S9.1.3: the snapshot retains the old value while the owner receives
// the appended replacement.
type Box = {values: array}

pn append(var values: array, value: int) int {
    push(values, value)
    return len(values)
}

pn update(var box: Box) int {
    var values: array = box.values
    var snapshot = values
    var count = append(values, 7)
    box.values = values
    print(count, " ", len(values), " ", len(snapshot), " ", len(box.values), "\n")
    return 0
}

pn main() {
    var box: Box = {values: []}
    var ignored = update(box)
    return "done"
}

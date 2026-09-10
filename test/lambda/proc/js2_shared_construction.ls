pn make_record(value) {
    return {value: value, next: null}
}
pn main() {
    let first = make_record(null)
    let second = make_record(5.0e-324)
    let third = make_record("text")
    print([first.value, second.value == 5.0e-324, third.value])
    var count = 0
    var total = 0.0
    while (count < 4) {
        total = total + abs(-2.5)
        count = count + 1
    }
    print(total)
}

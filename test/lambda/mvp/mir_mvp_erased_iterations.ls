// D5.4.4 / S9.1.2: physical loop guards must preserve open writes and snapshots.
pn scale(var data, factor) {
    var i = 0
    while (i < 4) {
        data[i] = data[i] * factor
        data[i + 1] = data[i + 1] * factor
        i = i + 2
    }
}
pn set_flags(var data) {
    var i = 0
    while (i < 4) {
        data[i] = false
        data[i + 1] = true
        i = i + 2
    }
}
pn copy_fields(data, var target) {
    var i = 0
    while (i < 2) {
        target.first = data[i]
        target.second = data[i + 1]
        i = i + 2
    }
}
pn selected(data: any, key: int) any {
    if (data[key]) { return true }
    return false
}
pn main() {
    var floats = [1.0, 2.0, 3.0, 4.0]
    scale(floats, 2.0)
    print(floats); print("\n")
    let snapshot = floats
    scale(floats, 0.5)
    print([floats, snapshot]); print("\n")
    var ints = [1, 2, 3, 4]
    scale(ints, 2.0)
    print(ints); print("\n")
    var mixed = [1.0, null, 3.0, 4.0]
    scale(mixed, 2.0)
    print(mixed); print("\n")
    var flags = [true, false, true, false]
    set_flags(flags)
    print(flags); print("\n")
    var short = [1.0, 2.0]
    scale(short, 2.0)
    print(short); print("\n")
    var record = {first: 0.0, second: 0.0}
    copy_fields([2.0, 3.0], record)
    print([record.first, record.second]); print("\n")
    print([selected([true], 0), selected([false], 0), selected([true], 1),
        selected([true], -1), selected([1], 0), selected([0], 0),
        selected([null], 0), selected([1.0], 0)]); print("\n")
}

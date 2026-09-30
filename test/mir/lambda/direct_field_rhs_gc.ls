// D4.4.4v4 / D5.3.1: a collecting RHS must finish before a packed field's
// data-zone address is loaded; bool stores touch only their one-byte slot.
type State = {value: int, flag: bool}

pn computed() int {
    let values = [10, 20, 30]
    return values[0] + values[1]
}

pn update(var record: State) {
    record.value = computed()
    record.flag = true
}

pn main() {
    var record: State = {value: 1, flag: false}
    update(record)
    print([record.value, record.flag])
}

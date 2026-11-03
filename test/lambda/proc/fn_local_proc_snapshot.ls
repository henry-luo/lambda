// S9.1.4v2: an escaping pn uses immutable creation-time captures.
fn reader(seed) {
    var value = seed
    pn confined() { value = value + 100 }
    let read = pn () => { return value }
    value = value + 7
    return read
}

pn main() {
    let read = reader(40)
    let read_array = array_reader()
    return [read(), read_array()]
}

fn array_reader() {
    var values = [1, 2]
    pn confined() { values[0] = 100 }
    let read = pn () => { return values[0] }
    values[0] = 9
    return read
}

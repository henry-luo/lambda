// D3.3.3v3: same-contract calls reuse admission; dynamic boundaries still convert.
type Vec = int[]

pn add(var values: Vec, value: int) any {
    push(values, value)
}

pn size(values: Vec) int { return len(values) }
pn create() Vec { return [1, 2] }
pn relay(var values: Vec) int {
    add(values, 3)
    return size(values)
}

pn dynamic_size(values) int { return size(values) }
pn dynamic_probe(values) int {
    if (true) { size(values) }
    return 7
}

type Saved = {values: Vec}
pn save(var saved: Saved, values: Vec) any { saved.values = values }
pn save_then_add(var values: Vec, var saved: Saved) any {
    save(saved, values)
    add(values, 8)
    push(values, 9)
}

pn main() {
    var values: Vec = create()
    let before = values
    print([relay(values), size(before), values[2]])
    print("\n")
    var rejected = false
    dynamic_size(["wrong"]) ^ { rejected = true }
    print([dynamic_size([4, 5]), rejected])
    print("\n")
    var interior_rejected = false
    dynamic_probe(["wrong"]) ^ { interior_rejected = true }
    var saved: Saved = {values: []}
    save_then_add(values, saved)
    print([saved.values, values, interior_rejected])
    print("\n")
    var i = 0
    while (i < 40) {
        add(values, i)
        i = i + 1
    }
    print([len(values), values[44], saved.values])
    print("\n")
}

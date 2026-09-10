type Values = {ints: int[], floats: float[], flags: bool[]}
pn append(var values: int[], value: int) { values.push(value) }
pn main() {
    var values: Values = {ints: [], floats: [], flags: []}
    var ints: int[] = values.ints
    append(ints, 7)
    values.ints = ints
    values.floats.push(2.5)
    values.flags.push(true)
    let snapshot = values
    append(ints, 9)
    print([values.ints[0], values.floats[0], values.flags[0],
        len(snapshot.ints), len(ints)])
}

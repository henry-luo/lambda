// S1.6/S9.1.2: a pure scalar key does not publish the array it indexes.
fn clamp_index(index: int, length: int) int =>
    if (index < 0) 0 else if (index >= length) length - 1 else index
pn accumulate(var values: float[], length: int) {
    var index: int = 0
    while (index < length) {
        values[index] = values[clamp_index(index - 1, length)] + 1.0
        index = index + 1
    }
}
pn main() {
    var values: float[] = [0.0, 0.0, 0.0, 0.0]
    let saved = values
    accumulate(values, 4)
    print([values, saved])
}

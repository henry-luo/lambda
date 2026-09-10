// S7.1.3v2: high index bits must not disappear in native bounds checks.
pn change(index: int) {
    var values: int[] = [0, 1]
    values[index] = 3
    return values
}
fn read(values: int[], index: int) => values[index]
fn text(values: string[], index: int) => values[index]
pn main() {
    var caught = 0
    change(4294967296) ^ { caught = caught + 1 }
    change(-4294967296) ^ { caught = caught + 1 }
    print([caught, read([2, 3], 4294967296), read([2, 3], -4294967296),
        text(["a", "b"], 4294967296), change(1)])
}

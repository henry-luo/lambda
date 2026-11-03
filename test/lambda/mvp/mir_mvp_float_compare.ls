// physical float guards retain absence, poison and alternate carriers (S6.1.2v2, S7.1.1v3).
pn comparisons(values: any, key: int, threshold: float) {
    print([values[key] < threshold, values[key] <= threshold, values[key] > threshold,
        values[key] >= threshold, values[key] == threshold, values[key] != threshold]); print("\n")
}
fn relation(values: any, key: int, threshold: float) any { return values[key] < threshold }
fn reversed(values: any, key: int, threshold: float) any { return threshold < values[key] }
pn find_prefix(values: any, threshold: float) int {
    var key: int = 0
    while (key < 3 and values[key] < threshold) { key = key + 1 }
    return key
}
pn change_kind(values: any) {
    var data = values
    let before = data[0] < 0.5
    data[0] = "changed"
    let after = data[0] < 0.5
    print([before, match after { case error: true default: false }]); print("\n")
}
pn main() {
    comparisons([0.5], 0, 0.5)
    comparisons([1], 0, 0.5)
    comparisons([nan], 0, 0.5)
    comparisons([0.5], 4294967296, 0.5)
    var mixed = fill(2, 0.5)
    mixed[0] = null
    comparisons(mixed, 0, 0.5)
    print([reversed([0.5], 0, 0.25), relation([0.5], 0, 0.25)]); print("\n")
    let invalid = relation(int("bad"), 0, 0.5)
    print(match invalid { case error: true default: false }); print("\n")
    change_kind([0.25])
    print([find_prefix([0.25, 0.75, 1.0], 0.5), find_prefix([nan], 0.5),
        find_prefix([null], 0.5), find_prefix([], 0.5)]); print("\n")
}

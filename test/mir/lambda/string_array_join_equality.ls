// S7.1.1v3/S5.1.1: typed string reads still yield null outside the array.
fn same(left: string[], right: string[], index: int) bool =>
    left[index] == right[index]

pn different(left: string[], right: string[], index: int) bool {
    let first = left[index]
    let second = right[index]
    return first != second
}

pn main() {
    let left: string[] = ["red", "blue"]
    let right: string[] = ["red", "green"]
    let short: string[] = ["red"]
    print([same(left, right, 0), same(left, right, 1),
        same(left, right, 2), same(left, short, 1),
        different(left, right, 1), different(left, right, 2)])
    print("\n")
}

// Tune31 follow-on / D3.3.3v3: a repeated text algorithm may admit a fixed
// string corpus once, then compare only the typed integer code lanes.

pn count_equal_codes(left: int[], right: int[]) int {
    var matches: int = 0
    var index: int = 0
    while (index < len(left) and index < len(right)) {
        if (left[index] == right[index]) {
            matches = matches + 1
        }
        index = index + 1
    }
    matches
}

pn main() {
    let left: int[] = [65, 66, 67, 68]
    let right: int[] = [65, 88, 67, 89]
    print(count_equal_codes(left, right))
}

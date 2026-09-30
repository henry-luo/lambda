// S9.1.2/S9.3.1: a string read needs no share mark, but a nullable or
// mismatched read still reaches the ownership helper.
pn capture_string(lines: string[], index: int) bool {
    let value = lines[index]
    var captured: array = []
    captured.push(value)
    return captured[0] == value
}

pn capture_dynamic(values: array, index: int) bool {
    let value = values[index]
    var captured: array = []
    captured.push(value)
    return captured[0] == value
}

pn main() {
    let lines: string[] = ["red", "blue"]
    print([capture_string(lines, 0), capture_string(lines, 3),
        capture_dynamic([[1]], 0)])
    print("\n")
}

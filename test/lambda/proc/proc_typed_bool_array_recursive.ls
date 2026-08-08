pn recurse(values: bool[], depth: int) int {
    if (depth == 0) {
        if (values[0] and values[1]) {
            return 1
        }
        return 0
    }
    return recurse(values, depth - 1)
}

pn main() {
    var values: bool[] = [true, true]
    print(recurse(values, 3))
    print(" ")
    values[1] = false
    print(recurse(values, 3))
    print("\n")
}

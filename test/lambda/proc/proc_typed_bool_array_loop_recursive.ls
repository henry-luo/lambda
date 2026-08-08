pn is_free(values, index: int) int {
    if (values[index]) {
        return 1
    }
    return 0
}

pn clear_first(values, index: int) {
    values[index] = false
}

pn choose(values: bool[], depth: int) int {
    for i in 0 to 7 {
        if (is_free(values, i) == 1) {
            clear_first(values, i)
            if (depth == 0) {
                return 1
            }
            if (choose(values, depth - 1) == 1) {
                return 1
            }
            values[i] = true
        }
    }
    return 0
}

pn main() {
    var values: bool[] = fill(8, true)
    print(choose(values, 1))
    print("\n")
}

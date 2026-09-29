pn prefix_count(values: int[], needle: int) int {
    var index = 0
    while (index < len(values) and values[index] == needle) {
        index = index + 1
    }
    index
}

pn negative_start(values: int[]) int {
    var index = -1
    while (index < len(values) and values[index] == 4) {
        index = index + 1
    }
    index
}

pn pair_prefix_count(left: int[], right: int[]) int {
    var index = 0
    while (index < len(right) and left[index] == right[index]) {
        index = index + 1
    }
    index
}

pn main() {
    let wide: int = 9007199254740991 + 1
    let poison: int = wide - wide
    print(prefix_count([4, 4, 6], 4)); print(" ")
    print(prefix_count([], 4)); print(" ")
    print(prefix_count([4, 4, 4], 4)); print(" ")
    print(prefix_count([poison], poison)); print(" ")
    print(negative_start([4, 4])); print(" ")
    print(pair_prefix_count([4, 4, 6], [4, 4, 7])); print(" ")
    print(pair_prefix_count([4], [4, 4])); print(" ")
    print(pair_prefix_count([poison], [poison])); print("\n")
}

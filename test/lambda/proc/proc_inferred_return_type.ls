pn next_seed(var seed: int[]) {
    var value: int = seed[0]
    value = value * 1309 + 13849
    value = int(value % 65536)
    seed[0] = value
    return value
}

pn build_label(limit: int) {
    var label: string = ""
    var index: int = 0
    while (index < limit) {
        label = label ++ "x"
        index = index + 1
    }
    return label
}

pn main() {
    var seed: int[] = [74755]
    let raw = next_seed(seed)
    print(int(raw % 500))
    let label: string = build_label(3)
    print(label)
}

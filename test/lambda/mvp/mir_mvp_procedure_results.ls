// S2.5.3v3/S12.1.2v3: a no-result procedure call still contributes null.
pn leaf(var values: int[], value: int) any { values[0] = value }
pn middle(var values: int[], value: int) any {
    42
    leaf(values, value)
    values[1] = value + 1
}
pn early(var values: int[], selected: bool) any {
    if (selected) { return 11 }
    leaf(values, 7)
}
pn scalar(var value: any, input: float) any { value = input }
pn explicit_result(var values: int[]) any {
    leaf(values, 8)
    return 7
}

pn main() {
    var values: int[] = [0, 0]
    let result = middle(values, 3)
    print([result == null, values[0], values[1]]); print("\n")
    print([early(values, true) == 11, early(values, false) == null]); print("\n")
    var empty: int[] = []
    print([explicit_result(values) == 7, leaf(empty, 3) is error]); print("\n")
    var wide: any = 0
    var saved: any = null
    var absent: any = 0
    var count: int = 1
    while (count <= 2) {
        absent = scalar(wide, float(count) * 1e-310)
        if (count == 1) { saved = wide }
        count = count + 1
    }
    print([absent == null, saved == 1e-310, wide == 2e-310]); print("\n")
}

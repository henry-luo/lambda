// S7.7.6v2: failed stores abandon the receiver's declaring block.
pn parameter(var values: int[]) {
    var i: int = 0
    while (i < 2) {
        values[0] = values[0] + 1
        values[9] = 0
        i = i + 1
    }
    return 5
}
pn outer_local() {
    var values: int[] = [1]
    if (true) {
        var i: int = 0
        while (i < 2) { values[9] = 0; i = i + 1 }
    }
    return 5
}
pn inner_local() {
    if (true) {
        var values: int[] = [1]
        var i: int = 0
        while (i < 2) { values[9] = 0; i = i + 1 }
        return 99
    }
    return 5
}
pn field_bad(var item: {value: int}, value: any) {
    var i: int = 0
    while (i < 2) { item.value = value; i = i + 1 }
    return 5
}
pn path_bad(var item: {child: {value: int}}, value: any) {
    var i: int = 0
    while (i < 2) { item.child.value = value; i = i + 1 }
    return 5
}
pn main() {
    var values: int[] = [1]
    let saved = values
    let failed = parameter(values)
    let outer = outer_local()
    let inner = inner_local()
    var item: {value: int} = {value: 7}
    let field = field_bad(item, "bad")
    var nested: {child: {value: int}} = {child: {value: 8}}
    let path = path_bad(nested, "bad")
    return [failed is error, values[0] == 2, saved[0] == 1,
        outer is error, inner == 5, field is error, item.value == 7,
        path is error, nested.child.value == 8]
}

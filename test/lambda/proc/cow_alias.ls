// Shared core containers detach at the mutating owner write.

pn main() {
    let flat_g = [1, 2]
    var flat_h = flat_g
    flat_h[0] = 99
    print((flat_g[0]) ++ " " ++ (flat_h[0]) ++ "\n")

    let map_g = {value: 1}
    var map_h = map_g
    map_h.value = 99
    print((map_g.value) ++ " " ++ (map_h.value) ++ "\n")

    var mutable_a = {value: 1}
    var mutable_b = mutable_a
    mutable_a.value = 17
    print((mutable_a.value) ++ " " ++ (mutable_b.value) ++ "\n")

    let pushed_g = []
    var pushed_h = pushed_g
    push(pushed_h, 2)
    print((len(pushed_g)) ++ " " ++ (len(pushed_h)) ++ "\n")

    let spliced_g = [{value: 1}, {value: 2}]
    var spliced_h = spliced_g
    splice(spliced_h, 0, 1)
    print((len(spliced_g)) ++ " " ++ (len(spliced_h)) ++ "\n")

    let g = [[1, 2], [3, 4]]
    var h = g
    h[0][0] = 99
    print((g[0][0]) ++ " " ++ (h[0][0]) ++ "\n")

    let nested_g = [{rows: [{value: 1}]}]
    var nested_h = nested_g
    nested_h[0].rows[0].value = 99
    print((nested_g[0].rows[0].value) ++ " " ++ (nested_h[0].rows[0].value) ++ "\n")
}

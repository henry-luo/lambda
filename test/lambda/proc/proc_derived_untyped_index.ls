// S7.1.3v2: derived dynamic keys remain checked. S9.1.2/S9.1.3: only var borrows write through.
pn set_sum(var xs, left, right, value) int^ {
    xs[left + right] = value
    return 1
}

pn set_typed_sum(var xs: int[], left, right, value) int^ {
    xs[left + right] = value
    return 1
}

pn set_nested(var box, left, right) int^ {
    box.rows[left * 2 + right] = 70
    return 1
}

pn set_snapshot(xs, left, right) {
    xs[left - right + 1] = 80
    return xs
}

pn main() {
    var xs = [10, 20, 30, 40]
    let old = xs
    set_sum(xs, 1, 1, 90)^
    print(string(xs) ++ " " ++ string(old) ++ "\n")
    print(string(set_snapshot(xs, 2, 1)) ++ " " ++ string(xs) ++ "\n")

    var typed: int[] = [1, 2, 3]
    let typed_old = typed
    set_typed_sum(typed, 1, 1, 9)^
    print(string(typed) ++ " " ++ string(typed_old) ++ "\n")

    var box = {rows: [1, 2, 3, 4]}
    let box_old = box
    set_nested(box, 1, 1)^
    print(string(box.rows) ++ " " ++ string(box_old.rows) ++ "\n")

    var rejected = 0
    set_sum(xs, 1.5, 0, 99) ^ { rejected = rejected + 1 }
    set_sum(xs, null, 0, 99) ^ { rejected = rejected + 1 }
    set_sum(xs, -2, 1, 99) ^ { rejected = rejected + 1 }
    set_sum(xs, 3, 1, 99) ^ { rejected = rejected + 1 }
    set_sum(xs, [0, 1], 0, 99) ^ { rejected = rejected + 1 }
    set_typed_sum(typed, 0, 1, "bad") ^ { rejected = rejected + 1 }
    print(string(rejected) ++ " " ++ string(xs) ++ " " ++ string(typed) ++ "\n")
}

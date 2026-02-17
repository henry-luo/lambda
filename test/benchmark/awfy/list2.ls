// AWFY Benchmark: List (Typed version)
// Expected result: 10

pn make_list(length: int) {
    if (length == 0) {
        return null
    }
    let e = {val: length, next: make_list(length - 1)}
    return e
}

pn list_length(node) {
    if (node == null) {
        return 0
    }
    return 1 + list_length(node.next)
}

pn is_shorter_than(x, y) {
    var x_tail = x
    var y_tail = y
    while (y_tail != null) {
        if (x_tail == null) {
            return 1
        }
        x_tail = x_tail.next
        y_tail = y_tail.next
    }
    return 0
}

pn tail(x, y, z) {
    if (is_shorter_than(y, x) == 1) {
        return tail(
            tail(x.next, y, z),
            tail(y.next, z, x),
            tail(z.next, x, y)
        )
    }
    return z
}

pn benchmark() {
    let result = tail(
        make_list(15),
        make_list(10),
        make_list(6)
    )
    return list_length(result)
}

pn main() {
    let result = benchmark()
    if (result == 10) {
        print("List: PASS\n")
    } else {
        print("List: FAIL result=")
        print(result)
        print("\n")
    }
}

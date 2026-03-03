// AWFY Benchmark: List (Typed version)
// Expected result: 10
// Typed: map? on nullable linked-list node params, int return types

pn make_list(length: int) {
    if (length == 0) {
        return null
    }
    let e = {val: length, next: make_list(length - 1)}
    return e
}

pn list_length(node: map?) int {
    if (node == null) {
        return 0
    }
    return 1 + list_length(node.next)
}

pn is_shorter_than(x: map?, y: map?) int {
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

pn tail(x: map?, y: map?, z: map?) {
    if (is_shorter_than(y, x) == 1) {
        return tail(
            tail(x.next, y, z),
            tail(y.next, z, x),
            tail(z.next, x, y)
        )
    }
    return z
}

pn benchmark() int {
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

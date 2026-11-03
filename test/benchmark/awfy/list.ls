// AWFY Benchmark: List (Tak function on linked lists)
// Expected result: 10
// Recursive linked-list traversal using Tak function

fn make_list(length) {
    if (length == 0) {
        return null
    }
    let e = {val: length, next: make_list(length - 1)}
    return e
}

fn list_length(node) any^ {
    if (node == null) {
        return 0
    }
    return 1 + list_length(node.next)
}

fn is_shorter_than(x, y) {
    var work_x = x
    var work_y = y

    while (work_y != null) {
        if (work_x == null) {
            return 1
        }
        work_x = work_x.next
        work_y = work_y.next
    }
    return 0
}

fn tail(x, y, z) any^ {
    if (is_shorter_than(y, x) == 1) {
        return tail(
            tail(x.next, y, z),
            tail(y.next, z, x),
            tail(z.next, x, y)
        )
    }
    return z
}

fn benchmark() any^ {
    let result = tail(
        make_list(15),
        make_list(10),
        make_list(6)
    )
    return list_length(result)
}

pn main() {
    var __t0 = clock()
    let result = benchmark()
    var __t1 = clock()
    if ((result == 10)^) {
        print("List: PASS\n")
    } else {
        print("List: FAIL result=")
        print(result)
        print("\n")
    }
    print("__TIMING__:" ++ ((__t1 - __t0) * 1000.0) ++ "\n")
}

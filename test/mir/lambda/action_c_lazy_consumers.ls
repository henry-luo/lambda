// Action C: shape-2 results resolve at arithmetic, truthiness, and type
// inspection consumers rather than at the direct-call boundary.

pn pending_item(value) {
    return value
}

pn consume_arithmetic(value) {
    return pending_item(value) + 1
}

pn consume_truth(value) {
    if (pending_item(value)) 1 else 0
}

pn consume_type(value) {
    return type(pending_item(value))
}

pn consume_compare(value) {
    if (pending_item(value) == 7i64) 1 else 0
}

pn main() {
    print(string(consume_arithmetic(7i64)) ++ "\n")
    print(string(consume_truth(7i64)) ++ "\n")
    print(string(consume_truth(0)) ++ "\n")
    print(string(consume_type(7i64)) ++ "\n")
    print(string(consume_compare(7i64)) ++ "\n")
    print(string(consume_compare(5i64)) ++ "\n")
}

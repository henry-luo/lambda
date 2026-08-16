// Action C: a pending direct result must be resolved before a dynamic call
// can publish it as an argument. The dynamic callee then returns normally.

pn pending_item(value) {
    return value
}

pn id(value) {
    return value
}

pn apply(f, value) {
    return f(value)
}

pn main() {
    print(string(apply(id, pending_item(7i64))) ++ "\n")
}

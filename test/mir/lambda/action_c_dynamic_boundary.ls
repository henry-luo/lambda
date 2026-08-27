// Action C: a pending direct result must be resolved before a dynamic call
// can publish it as an argument. The dynamic callee then returns normally.

pn pending_item(value) {
    return value
}

pn id(value) {
    return value
}

pn apply_fn(f, value) {
    return f(value)
}

pn main() {
    print(string(apply_fn(id, pending_item(7i64))) ++ "\n")
}

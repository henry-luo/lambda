// Action C: a same-shape direct return forwards both result lanes through the
// caller epilogue; only the public wrapper resolves the incompatible boundary.

pn leaf(value) {
    return value
}

pn forward(value) {
    return leaf(value)
}

pn main() {
    print(string(forward(7i64)) ++ "\n")
}

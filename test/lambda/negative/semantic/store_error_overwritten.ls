// S7.5.5v2: forward calls must retain a rejected store's error outcome.
pn main() {
    write_local(5)
    print("unreachable")
}

pn write_local(index) {
    var xs = [1]
    xs[index] = 9
    return 23
}

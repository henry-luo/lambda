// S7.1.3v3/S7.7.6v2: main must surface a rejected unannotated store.
pn main() {
    var b = [1]
    b[5] = 2
    print("unreachable")
}

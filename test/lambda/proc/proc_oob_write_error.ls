pn write_oob() int^ {
    var xs = [1, 2, 3]
    xs[10] = 99
    xs[0]
}

pn main() {
    var err = null
    write_oob() ^ { err = ^ }
    print(err is error)
    print("\n")
}

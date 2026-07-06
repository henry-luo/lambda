pn write_oob() int^ {
    var xs = [1, 2, 3]
    xs[10] = 99
    xs[0]
}

pn main() {
    let value^err = write_oob()
    print(^err)
    print("\n")
}


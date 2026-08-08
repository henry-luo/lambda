pn bump(values) {
    values[0] = values[0] + 1
}

pn main() {
    var values = [1, 2]
    bump(values)
    print(values[0])
    print(" ")
    print(values[1])
    print("\n")
}

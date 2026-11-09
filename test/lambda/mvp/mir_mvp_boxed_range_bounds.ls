// D2.3.5/S8.3.1v3: generic len outcomes need native lanes at counted loops.
pn count(values) int {
    var total = 0
    for (index in 0 to (len(values) - 1)) { total = total + index + 1 }
    return total
}
pn count_after(prefix, values) int {
    var total = 0
    for (index in len(prefix) to (len(values) - 1)) { total = total + index + 1 }
    return total
}
pn main() {
    print([count([1, 2, 3]), count([]), count("abcd"),
        count_after([0], [1, 2, 3]), count_after([], [1, 2, 3])])
}

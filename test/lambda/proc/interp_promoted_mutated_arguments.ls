// S9.1.2/D8.3.3: entry into a promoted mutator snapshots its plain argument.
pn mutate(values: bool[]) int^ {
    values[0] = true
    return if (values[0]) 1 else 0
}
pn main() {
    var values: bool[] = fill(3, false)^
    var count = 0
    for (i in 0 to 20) {
        values[1] = true
        count = count + mutate(values)^
    }
    print([count, values])
}

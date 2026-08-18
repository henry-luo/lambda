// Regression: a module-level typed array read from a procedure must preserve
// the Item carrier when the MIR binding has no local element witness.
let proc_global_values: int[] = [2, 6, 6, 7]

pn read_first() int {
    return proc_global_values[0]
}

pn read_offset() int {
    return proc_global_values[1] + 1
}

pn main() {
    print([read_first(), read_offset()])
}

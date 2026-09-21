// Tune31 Phase II F / S7.1.3v2: an inferred ArrayNum witness is only a fast
// success arm. Null writes and out-of-range float writes keep the checked
// representation-changing and error paths.

pn write_null(var values) {
    values[0] = null
}

pn write_oob(var values) any^ {
    values[3] = 7.0
}

pn main() {
    var values = [1.0, 2.0]
    write_null(values)
    var oob = false
    write_oob(values) ^ { oob = true }
    print(values); print(" "); print(oob); print("\n")
}

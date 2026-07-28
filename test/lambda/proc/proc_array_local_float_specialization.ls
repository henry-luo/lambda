// A local float literal remains an ArrayNum after a representation-preserving
// store, so indexed reads may stay in the native double lane.

pn local_float_total() {
    var values = [1.25, 2.5, 3.75]
    values[1] = 2.25
    var total = 0.0
    var i = 0
    while (i < 3) {
        total = total + values[i]
        i = i + 1
    }
    return total
}

pn main() {
    print(local_float_total())
}

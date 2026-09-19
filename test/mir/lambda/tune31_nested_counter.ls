// Tune31 T31-1 / S4.1.1-S4.1.5: a compact outer-loop update must not claim
// the distinct nested counter initializer. Both counters remain native, while
// the source result still observes their ordinary integer semantics.

pn nested_counter(limit) {
    var total = 0
    var i = 0
    while (i < limit) {
        var j = i + 1
        while (j < limit) {
            total = total + j
            j = j + 1
        }
        i = i + 1
    }
    return total
}

pn main() {
    print(nested_counter(5))
    print("\n")
}

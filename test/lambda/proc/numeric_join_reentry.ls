// D2.4.1-D2.4.3: scalar-carrier proofs must terminate on local recurrences.
pn bounded_min(limit) {
    var total = 0
    while (total < limit) {
        let step = min(2, limit - total)
        total = total + step
    }
    return total
}

pn bounded_max(limit) {
    var total = 0
    while (total < limit) {
        let step = max(1, (limit - total) div 2)
        total = total + step
    }
    return total
}

pn bounded_float(limit) {
    var total = 0.0
    while (total < limit) {
        let step = min(0.75, limit - total)
        total = total + step
    }
    return total
}

pn nested_math(limit) {
    var total = 0
    while (total < limit) {
        let step = min(2, abs(limit - total))
        total = total + step
    }
    return total
}

pn main() {
    print([bounded_min(5) == 5, bounded_min(0) == 0, bounded_min(19) == 19,
        bounded_max(9) == 9, bounded_float(2.0) == 2.0, nested_math(7) == 7])
    print("\n")
}

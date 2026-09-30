// S7.1.1v3/D2.5.3: the loop head proves each stable a[i] body read until
// the sole tail increment. Another array or an earlier index write declines.
pn sum(a: float[]) float {
    var i: int = 0
    var total: float = 0.0
    while (i < len(a)) {
        total = total + a[i]
        i = i + 1
    }
    return total
}

pn mismatched(a: float[], b: float[]) float? {
    var i: int = 0
    var seen: float? = null
    while (i < len(b)) {
        seen = a[i]
        i = i + 1
    }
    return seen
}

pn early_write(a: float[]) float? {
    var i: int = 0
    var seen: float? = null
    while (i < len(a)) {
        i = i + 1
        seen = a[i]
        i = i + 1
    }
    return seen
}

pn resized(a: float[]) float? {
    var i: int = 0
    var seen: float? = null
    while (i < len(a)) {
        a = []
        seen = a[i]
        i = i + 1
    }
    return seen
}

pn main() {
    print(sum([1.0, 2.0, 3.0])); print(" ")
    print(sum([])); print(" ")
    print(mismatched([1.0], [9.0, 8.0])); print(" ")
    print(mismatched([1.0, 2.0], [9.0])); print(" ")
    print(early_write([1.0])); print(" ")
    print(resized([1.0])); print("\n")
}

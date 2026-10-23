pn half_walk(n: int) {
    var j = 0
    var i = 0
    var trace = []
    while (i < 4) {
        var m = n / 2
        trace = trace ++ [[j, type(j), i < j, j >= m]]
        while (m >= 2 and j >= m) {
            j = j - m
            m = m / 2
        }
        j = j + m
        i = i + 1
    }
    return [trace, j, type(j)]
}
pn may_widen(flag) {
    var x = 0
    var i = 0
    while (i < 2) {
        if (flag) { x = null } else { x = x + 0.5 }
        i = i + 1
    }
    return [x, x + 1.0]
}
pn member_tags(left_float, right_float) {
    var left = 4
    var right = 2
    if (left_float) { left = left / 2 }
    if (right_float) { right = right / 2 }
    let sum = left + right
    let difference = left - right
    let product = left * right
    return [sum, type(sum), difference, type(difference), product, type(product)]
}
pn member_overflow(floating) {
    var left = 9007199254740991
    var right = 1
    if (floating) { left = left / 2; right = right / 2 }
    let sum = left + right
    return [sum, type(sum)]
}
pn main() {
    print(half_walk(8)); print("\n")
    print(half_walk(5)); print("\n")
    print(half_walk(-2)); print("\n")
    print([may_widen(false), may_widen(true)]); print("\n")
    print([member_tags(false, false), member_tags(false, true)]); print("\n")
    print([member_tags(true, false), member_tags(true, true)]); print("\n")
    print([member_overflow(false), member_overflow(true)]); print("\n")
}

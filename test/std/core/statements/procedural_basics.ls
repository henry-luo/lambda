// Test: Procedural Basics
// Layer: 2 | Category: statement | Covers: var, assignment, while, break, continue, return
// Mode: procedural

pn main() {
    // ===== var declaration =====
    var x = 10
    print(x)

    // ===== var reassignment =====
    x = 20
    print(x)

    // ===== Multiple vars =====
    var a = 1
    var b = 2
    var c = a + b
    print(c)

    // ===== While loop =====
    var i = 0
    var sum = 0
    while (i < 5) {
        sum = sum + i
        i = i + 1
    }
    print(sum)

    // ===== While with break =====
    var j = 0
    while (true) {
        if (j >= 3) break
        j = j + 1
    }
    print(j)

    // ===== While with continue =====
    var k = 0
    var even_sum = 0
    while (k < 10) {
        k = k + 1
        if (k % 2 != 0) continue
        even_sum = even_sum + k
    }
    print(even_sum)

    // ===== Nested while =====
    var product = 0
    var m = 1
    while (m <= 3) {
        var n = 1
        while (n <= 3) {
            if (m == n) product = product + 1
            n = n + 1
        }
        m = m + 1
    }
    print(product)

    // ===== Return statement =====
    print(early_return(5))
    print(early_return(-1))

    // ===== Var type widening =====
    var val: int | string = 42
    print(val)
    val = "hello"
    print(val)
}

pn early_return(x: int) int {
    if (x < 0) return -1
    return x * 2
}

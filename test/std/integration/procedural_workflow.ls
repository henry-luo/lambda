// Test: Procedural Workflow
// Layer: 4 | Category: integration | Covers: pn, var, while, mutation, clock
// Mode: procedural

pn main() {
    // ===== Accumulate computation =====
    var total = 0
    var i = 1
    while (i <= 100) {
        total = total + i
        i = i + 1
    }
    print("Sum 1-100: " ++ string(total))

    // ===== Build array iteratively =====
    var fibonacci = [1, 1]
    while (len(fibonacci) < 20) {
        let n = len(fibonacci)
        fibonacci = [*fibonacci, fibonacci[n - 1] + fibonacci[n - 2]]
    }
    print("Fibonacci length: " ++ string(len(fibonacci)))
    print("Fibonacci[19]: " ++ string(fibonacci[19]))

    // ===== Map building =====
    var counts = {}
    let words = ["apple", "banana", "apple", "cherry", "banana", "apple"]
    var w = 0
    while (w < len(words)) {
        let word = words[w]
        let current = counts[word] or 0
        counts[word] = current + 1
        w = w + 1
    }
    print("apple count: " ++ string(counts.apple))
    print("banana count: " ++ string(counts.banana))
    print("cherry count: " ++ string(counts.cherry))

    // ===== Nested loops =====
    var matrix = []
    var r = 0
    while (r < 3) {
        var row = []
        var c = 0
        while (c < 3) {
            row = [*row, (r + 1) * (c + 1)]
            c = c + 1
        }
        matrix = [*matrix, row]
        r = r + 1
    }
    print("Matrix[0]: " ++ string(matrix[0]))
    print("Matrix[2]: " ++ string(matrix[2]))

    // ===== Early return with condition =====
    print("Is prime 7: " ++ string(is_prime(7)))
    print("Is prime 10: " ++ string(is_prime(10)))
    print("Is prime 97: " ++ string(is_prime(97)))

    // ===== String building =====
    var result = ""
    var k = 0
    while (k < 5) {
        if (k > 0) { result = result ++ ", " }
        result = result ++ string(k * k)
        k = k + 1
    }
    print("Squares: " ++ result)
}

pn is_prime(n: int) bool {
    if (n < 2) { return false }
    var i = 2
    while (i * i <= n) {
        if (n % i == 0) { return false }
        i = i + 1
    }
    return true
}

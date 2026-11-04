// D5.4.4, S7.1.1v3: whole-loop guards preserve shared owners and checked misses.
pn evolve(var a: int[], b: int[], repeats: int) int {
    var marker: int[] = [1]
    for pass in 0 to repeats - 1 {
        for i in 0 to 3 { a[i] = a[i] + b[i] }
    }
    return marker[0]
}
pn replacing(var a: int[]) {
    for pass in 0 to 1 {
        a = fill(4, pass)^
        for i in 0 to 3 { a[i] = a[i] + 2 }
    }
}
// S4.1.2: nonnegative offsets tighten a positive counter bound; negative offsets stay checked.
pn triangular(limit: int) int {
    var count: int = 0
    var i: int = 0
    while (i * 3 <= limit) {
        var j: int = i
        while (i + j * 2 <= limit) { count = count + 1; j = j + 1 }
        i = i + 1
    }
    return count
}
pn shifted(offset: int, limit: int) int {
    var i: int = 0
    while (offset + i * 2 <= limit) { i = i + 1 }
    return i
}
pn polynomial(x: int) int {
    return int(x * (x + 1) * (x + 2) div 6)
}
pn changed_after_condition() int {
    var x: int = 0
    if (x < 1) {
        x = 9007199254740991
        return x + 1
    }
    return 0
}
pn conditional_indices(var a: int[]) int^ {
    var marker: int[] = [1]
    var sum: int = 0
    for i in 0 to 4 {
        var k: int = i - 1
        if (k >= 0 and k < 3) { sum = sum + a[k] }
        if (k < 0 or k >= 3) {} else { a[k] = a[k] + 1 }
    }
    return sum + marker[0]
}
pn reverse(var a: int[], lo: int, hi: int) int {
    while (lo < hi) {
        var tmp = a[lo]
        a[lo] = a[hi]
        a[hi] = tmp
        lo = lo + 1
        hi = hi - 1
    }
    return lo * 10 + hi
}
pn reverse_float(var a: float[], lo: int, hi: int) {
    while (lo < hi) {
        var tmp: float = a[lo]
        a[lo] = a[hi]
        a[hi] = tmp
        lo = lo + 1
        hi = hi - 1
    }
}
pn classify(a: int, b: int, c: int) int {
    var result: int = 0
    if (a == b and b == c) { result = result + 1 }
    if (a == b and b != c) { result = result + 2 }
    if (a != b and b == c) { result = result + 4 }
    if (a != b and b != c) { result = result + 8 }
    return result
}
pn changing_comparisons() int {
    var a: int = 1
    var b: int = 1
    var c: int = 1
    var result: int = 0
    if (a == b and b == c) { result = result + 1; b = 2 }
    if (a == b and b != c) { result = result + 2 }
    if (a != b and b == c) { result = result + 4 }
    if (a != b and b != c) { result = result + 8 }
    return result
}
pn float_branches(x: float) int {
    var i: int = 0
    var result: int = 0
    while (i < 1 and x != 0.0) { result = 1; i = i + 1 }
    if (x == 0.0) { result = result + 2 }
    if (x != 0.0) { result = result + 4 }
    if (x < 0.0) { result = result + 8 }
    if (x > 0.0) { result = result + 16 }
    return result
}
pn multiply_two(x: int) int { return x * 2 }
pn multiply_zero(x: int) int { return x * 0 }
pn nested_count() int {
    var total: int = 0
    for i in 0 to 2 {
        var j: int = 0
        while (j < 4) { total = total + 1; j = j + 1 }
    }
    return total
}
pn nested_overflow() int {
    var total: int = 9007199254740989
    for i in 0 to 1 {
        var j: int = 0
        while (j < 3) { total = total + 1; j = j + 1 }
    }
    return total
}
pn reset_overflow() int {
    var total: int = 0
    var seen: int = 0
    for i in 0 to 1 {
        total = total + 1
        seen = total
        total = 9007199254740991
    }
    return seen
}
pn contracting_difference() int {
    var i: int = 0
    var j: int = 0
    while (i < 8) {
        var m: int = 8
        while (m >= 1 and j >= m) {
            j = j - m
            m = int(m div 2)
        }
        j = j + 3
        i = i + 1
    }
    return j
}
pn main() {
    var a: int[] = [1, 2, 3, 4]
    var b: int[] = [1, 2, 3, 4]
    let saved: int[] = a
    evolve(a, b, 2)^
    print(a); print(" "); print(saved); print("\n")
    var short: int[] = [1, 2]
    evolve(short, b, 2)^
    print(short); print("\n")
    evolve(a, short, 0)^
    print(a); print("\n")
    replacing(a)^
    print(a); print("\n")
    print(triangular(8)); print(" "); print(triangular(0)); print(" "); print(triangular(-1)); print("\n")
    print(shifted(3, 7)); print(" "); print(shifted(-3, 7)); print("\n")
    var values: int[] = [3, -3, 200000, 1000000, int(0.0 / 0.0)]
    for i in 0 to 4 { if (i > 0) { print(" ") }; print(polynomial(values[i])) }
    print("\n")
    print(changed_after_condition()); print("\n")
    var indexed: int[] = [2, 3, 4]
    print(conditional_indices(indexed)); print(" "); print(indexed); print("\n")
    var reversed: int[] = [1, 2, 3, 4, 5]
    let original: int[] = reversed
    print(reverse(reversed, 0, 4)); print(" "); print(reversed); print(" "); print(original); print("\n")
    print(reverse(reversed, 1, 3)); print(" "); print(reversed); print("\n")
    print(reverse(reversed, 2, -1)); print(" "); print(reversed); print("\n")
    reverse(short, 0, 3); reverse(short, -1, 1)
    print(short); print("\n")
    var floating: float[] = [-0.0, 1.5, 0.0, 0.0 / 0.0]
    reverse_float(floating, 0, 3)
    print(floating[0] != floating[0]); print(" "); print(1.0 / floating[3] < 0.0); print(" "); print(floating[2]); print("\n")
    print(classify(1, 1, 1)); print(" "); print(classify(1, 1, 2)); print(" ")
    print(classify(1, 2, 2)); print(" "); print(classify(1, 2, 3)); print(" ")
    print(classify(int(0.0 / 0.0), int(0.0 / 0.0), int(0.0 / 0.0))); print(" ")
    print(changing_comparisons()); print("\n")
    print(float_branches(0.0)); print(" "); print(float_branches(1.0)); print(" ")
    print(float_branches(-1.0)); print(" "); print(float_branches(0.0 / 0.0)); print("\n")
    print(multiply_two(int(1.0 / 0.0))); print(" "); print(multiply_two(int(-1.0 / 0.0))); print(" ")
    print(multiply_two(int(0.0 / 0.0))); print(" "); print(multiply_zero(int(1.0 / 0.0))); print("\n")
    print(nested_count()); print(" "); print(nested_overflow()); print(" ")
    print(reset_overflow()); print(" "); print(contracting_difference()); print("\n")
}

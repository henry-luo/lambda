// S7.1.1v3, S9.1.2: iteration guards precede writes and preserve checked misses and snapshots.
pn butterfly(var a: float[], n: int, stride: int) {
    var i: int = 0
    while (i < n) {
        var j: int = i + stride
        var x = a[i]
        var y = a[j]
        a[i] = x + y
        a[j] = x - y
        var xi = a[i + 1]
        var yi = a[j + 1]
        a[i + 1] = xi + yi
        a[j + 1] = xi - yi
        i = i + stride + stride
    }
}
pn changing_index(var a: int[]) {
    var i: int = 0
    while (i < 2) {
        a[i] = a[i] + 1
        i = i + 1
        a[i] = a[i] + 2
    }
}
pn scale_end(start: int, end: int, scale: int) int {
    var i: int = start
    while (i < end) { i = i * scale }
    return i
}
pn contract_end(start: int) int {
    var n: int = start
    while (n > 0) { n = int(n div 2) }
    while (n < 0) { n = int(n div 2) }
    return n
}
// S7.1.1v3, S9.1.2: complete-loop guards retain tails, empty loops and snapshots.
pn stride_rows(var a: float[], b: float[], first: int, end: int, step: int) int {
    var i: int = first
    while (i < end) {
        a[i] = b[i] + b[i + 1]
        i = i + step
    }
    return i
}
pn moving_limit(var a: float[], end: int) {
    var i: int = 0
    var limit: int = end
    while (i < limit) {
        a[i] = a[i] + a[i + 1]
        limit = limit - 1
        i = i + 1
    }
}
pn text_matches(a: string, b: string, start: int, end: int) int {
    var i: int = start
    var count: int = 0
    while (i < end) {
        if (a[i] == b[i]) { count = count + 1 }
        i = i + 1
    }
    return count
}
pn fixed_character_matches(a: string, b: string, row: int, end: int) int {
    var i: int = 0
    var count: int = 0
    while (i < end) {
        if (a[row] == b[i]) { count = count + 1 }
        i = i + 1
    }
    return count
}
pn conditional_swap(var a: float[], i: int, j: int, enabled: bool) {
    if (enabled) {
        let saved = a[i]
        a[i] = a[j]
        a[j] = saved
    } else {
        a[0] = a[0] + 1.0
        a[1] = a[1] + 2.0
    }
}
pn main() {
    var a: float[] = [1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0]
    let saved: float[] = a
    butterfly(a, 8, 2)
    print(a); print(" "); print(saved); print("\n")
    var short: float[] = [1.0]
    butterfly(short, 2, 2)
    print(short); print("\n")
    butterfly(short, 1, -1)
    print(short); print("\n")
    var b: int[] = [1, 2, 3]
    changing_index(b)
    print(b); print("\n")
    print(scale_end(2, 17, 2)); print(" "); print(scale_end(3, 17, 3)); print(" ")
    print(contract_end(1023)); print(" "); print(contract_end(-1023)); print("\n")
    var positive: float[] = fill(2, 0.0)
    var negative: float[] = fill(2, -0.0)
    print(1.0 / positive[1] > 0.0); print(" "); print(1.0 / negative[1] < 0.0); print("\n")
    print(string(-9007199254740991)); print(" "); print(string(9007199254740991)); print(" ")
    print(string(0)); print(" "); print(string(int(1.0 / 0.0))); print("\n")
    print(int(-2.75)); print(" "); print(int(2.75)); print(" "); print(int(-1.0 / 0.0)); print("\n")
    print(dispatch(1)); print(" "); print(dispatch(2)); print(" "); print(dispatch(3)); print(" ")
    print(dispatch(4)); print(" "); print(dispatch(int(0.0 / 0.0))); print(" "); print(changing_selector()); print("\n")
    var swapping: float[] = [1.0, 2.0, 3.0]
    conditional_swap(swapping, 0, 2, true)
    print(swapping); print("\n")
    let snapshot: float[] = swapping
    conditional_swap(swapping, 0, 1, true)
    conditional_swap(swapping, 0, 9, true)
    print(swapping); print(" "); print(snapshot); print("\n")
    conditional_swap(swapping, 0, 0, false)
    print(swapping); print("\n")
    var partial: float[] = [1.0]
    conditional_swap(partial, 0, 0, false)
    print(partial); print("\n")
    var lanes: float[] = [1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0]
    let original: float[] = lanes
    print(stride_rows(lanes, original, 0, 8, 3)); print(" "); print(lanes); print("\n")
    print(stride_rows(lanes, original, 0, 6, 4)); print(" "); print(lanes); print("\n")
    print(stride_rows(lanes, original, 5, 2, 1)); print(" "); print(original); print("\n")
    moving_limit(lanes, 7)
    print(lanes); print("\n")
    stride_rows(lanes, original, -1, 1, 1)
    stride_rows(lanes, partial, 0, 3, 1)
    print(stride_rows(lanes, original, 3, 1, 0)); print(" "); print(lanes); print("\n")
    print(text_matches("abc", "axc", 0, 3)); print(" ")
    print(text_matches("éλz", "éμz", 0, 3)); print(" ")
    print(text_matches("ab", "ab", -1, 2)); print(" ")
    print(text_matches("a", "ab", 0, 3)); print("\n")
    print(text_matches("a\u0000", "a", 0, 2)); print(" ")
    print(text_matches("a\u0000", "a\u0000", 0, 2)); print(" ")
    print(fixed_character_matches("ax", "aab", 0, 3)); print(" ")
    print(fixed_character_matches("é", "éaé", 0, 3)); print(" ")
    print(fixed_character_matches("\u0000", "a\u0000b", 0, 3)); print(" ")
    print(fixed_character_matches("a", "", -1, 2)); print("\n")
}
pn dispatch(op: int) int {
    var result: int = 0
    if (op == 1) { result = result + 1 }
    if (op == 2) { result = result + 2 }
    if (op == 3) { result = result + 3 }
    if (op == 2) { result = result + 10 }
    return result
}
pn changing_selector() int {
    var op: int = 1
    var result: int = 0
    if (op == 1) { op = 2; result = result + 1 }
    if (op == 2) { op = 3; result = result + 2 }
    if (op == 3) { result = result + 3 }
    return result
}

// D7.4.1/D6.3: entry predictions retain fallback effects and precise owners.
fn float_summary(x: any, y: any) any {
    var delta = x - y
    var square = delta * delta + 1.0
    return math.sqrt(square)
}
fn text_summary(left: any, right: any) any^ {
    let n = len(left)^
    let m = len(right)^
    let size = n + m
    var slots: int[] = fill(size, 0)^
    var count = 0
    var i = 0
    while (i < n and i < m) {
        if (left[i] == right[i]) { count = count + 1 }
        i = i + 1
    }
    let saved = slots
    slots[0] = count
    return [count, saved[0], slots[0], n, m]
}
fn mixed(value: any, change: bool) any {
    var result = len(value)
    if (change) { result = "changed" }
    return result
}
fn choose(flag) { return if (flag) "yes" else "no" }
fn string_size(value) { return len(value) }
pn effect(value: any) any { print("visit\n"); return value + 1.0 }
pn invalid_store(value: any) any { value.field = 3; return value }
pn invalid_local(value: any) any { var local = value; local.field = 3; return local }
pn main() {
    let floats = [[3.0, 3.0]]
    let texts = [["ab", "ac"]]
    print([float_summary(floats[0][0], floats[0][1]), float_summary(2, 2),
        float_summary("bad", 1.0) is error, float_summary(null, 1.0)]); print("\n")
    print(text_summary(texts[0][0], texts[0][1])); print("\n")
    print(text_summary([1, 2], [1, 3])); print("\n")
    print(text_summary("éλ", "éβ")); print("\n")
    print([mixed(texts[0][0], true), mixed([1, 2, 3], false),
        choose(true), choose(false), string_size("éλ")]); print("\n")
    let first = effect(floats[0][0])
    let second = effect(2)
    print([first, second]); print("\n")
    // a raw string's length word can resemble a container kind.
    let suspicious = [["0123456789abcdefg"]]
    let invalid = invalid_store(suspicious[0][0])
    let invalid_copy = invalid_local(suspicious[0][0])
    print([invalid is error, invalid_copy is error]); print("\n")
}

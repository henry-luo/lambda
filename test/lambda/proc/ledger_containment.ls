// S7.7.2v2/S7.7.4v2: local outcomes continue; field stores retain containment.
fn checked(x: int) int => x + 1
pn arguments(x) {
    let direct: any^ = checked(x)
    let invoke = checked
    let dynamic: any^ = invoke(x)
    print([direct is error, dynamic is error, checked(2)])
    print("\n")
}
pn nested_declaration(x) {
    if (true) {
        let n: int = x
        print(["declaration continued", n is error])
        print("\n")
    }
    print("declaration after")
    print("\n")
}
pn nested_assignment(x) {
    if (true) {
        var n: int = 1
        n = x
        print(["assignment continued", n is error])
        print("\n")
    }
    print("assignment after")
    print("\n")
}
pn multiple_bindings(x) {
    if (true) {
        var a: int = 1, b: int = 2
        b = x
        print(["second binding continued", b is error])
        print("\n")
    }
    print("second binding after")
    print("\n")
}
// A bound error returns only through the explicit return, even when inlined.
pn inline_nested(a: int[], i: int) {
    if (true) { let n: int = a[i]; return n }
    return 7
}
pn inline_store(var a: int[], i: int) { a[0] = a[i] }
pn outer_assignment(x) {
    var n: int = 1
    if (true) { n = x }
    print(["outer assignment continued", n is error])
    print("\n")
}
fn batch(xs) => [for (x in xs) { let n: int = x; n + 1 }]
fn block_value(x) {
    let r = { let n: int = x; n + 1 };
    [r is error, r + 1, {value: r}]
}
pn store(var a: int[], x) {
    a[0] = 3
    a[1] = x
    print("unreachable store")
}
pn local_store(x) {
    if (true) {
        var a: int[] = [1, 2]
        a[0] = x
        print("unreachable local store")
    }
    print("store after")
    print("\n")
}
type Pair = {n: int, label: string}
fn make_pair(x: int) Pair => {n: x, label: "ok"}
fn field_pair(x) Pair => {n: checked(x), label: "ok"}
pn record_result(x) {
    let r = make_pair(x)
    print("record after")
    print("\n")
    print(r.n)
    print("\n")
}
pn record_field_result(x) {
    let r = field_pair(x)
    print("field after")
    print("\n")
    print(r.n)
    print("\n")
}
pn null_argument(xs: int[], i: int) {
    let result: any^ = checked(xs[i])
    print([result is error, "null argument after"])
    print("\n")
}
pn child() {
    sleep(100)^
    print("child completed")
    print("\n")
}
pn scoped_failure(x) {
    if (true) {
        start(child, [])
        let n: int = x
        print(["task scope continued", n is error])
        print("\n")
    }
    print("task after")
    print("\n")
}
pn main() {
    arguments("s")
    null_argument([1, 2], 99)
    scoped_failure("s")
    nested_declaration("s")
    nested_assignment("s")
    multiple_bindings("s")
    var inline_values: int[] = [1]
    print(inline_nested(inline_values, 99))
    print("\n")
    let inline_failure = inline_store(inline_values, 99)
    print([inline_failure is error, inline_values])
    print("\n")
    let outer = outer_assignment("s")
    print(outer is error)
    print("\n")
    print(batch([1, "s", 3]))
    print("\n")
    print(block_value("s"))
    print("\n")
    var a: int[] = [1, 2]
    let failed = store(a, "s")
    print([failed is error, a])
    print("\n")
    local_store("s")
    record_result("s")
    record_field_result("s")
}

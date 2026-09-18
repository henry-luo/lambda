// T29-4: a typed `int[]` local store whose value is an Item (an untyped call
// result) tests the value's int tag inline and stores it directly; only a
// non-int value reaches the checked setter, which still rejects it.
fn mix(x) => x * 7

pn fill_mod(n: int) int {
    var bx: int[] = fill(n, 0)
    for i in 0 to n - 1 {
        bx[i] = mix(i) % 500
    }
    var total = 0
    for i in 0 to n - 1 {
        total = total + bx[i]
    }
    return total
}

fn pick(v) => v

pn store_any(v) int {
    var a: int[] = fill(2, 0)
    a[1] = pick(v)
    return a[1]
}

pn main() {
    print("sum=" ++ fill_mod(100) ++ "\n")
    print("int=" ++ store_any(41) ++ "\n")
    let s = store_any("s")
    print("string_rejected=" ++ (s is error) ++ "\n")
    let f = store_any(2.5)
    print("float=" ++ (if (f is error) "rejected" else string(f)) ++ "\n")
    let big = store_any(9007199254740993i64)
    print("big=" ++ (if (big is error) "rejected" else string(big)) ++ "\n")
}

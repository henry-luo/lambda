// S8.2.4/D3.3.3: numeric keys must be exact positions; erased element lanes remain Items.
pn read_numeric(a, key: float) { return a[key] }
pn mixed(n: int) {
    var key = 0
    var i = 0
    let a = [10, 20, 30, 40]
    var result = []
    while (i < 4) {
        result = result ++ [a[key]]
        key = key + n / 2
        i = i + 1
    }
    return result
}
pn erase(flag) {
    var values = fill(3, 7)
    if (flag) { values[1] = "wide" }
    return [values[0], values[1], values[2], values[3]]
}
pn local_values(key: float) { var values = fill(3, 2.5); return values[key] }
pn effectful_array() { print("object\n"); return [10, 20, 30] }
pn effectful_key() float { print("key\n"); return 1.0 }
pn rebinding_key(var values) float { values[1] = 99; return 1.0 }
pn main() {
    print([local_values(1.0), local_values(1.5)]); print("\n")
    print(effectful_array()[effectful_key()]); print("\n")
    var observed = [10, 20, 30]
    print([observed[rebinding_key(observed)], observed]); print("\n")
    print(["abc"[1.5], "abc"[1.0]]); print("\n")
    let a = fill(3, 2.5)
    print([read_numeric(a, 0.0), read_numeric(a, 2.0), read_numeric(a, 1.5)]); print("\n")
    print([read_numeric(a, -0.5), read_numeric(a, -1.0), read_numeric(a, inf), read_numeric(a, nan)]); print("\n")
    print([mixed(2), mixed(1)]); print("\n")
    print([erase(false), erase(true)]); print("\n")
}

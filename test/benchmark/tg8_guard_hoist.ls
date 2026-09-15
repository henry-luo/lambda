// D8.3.4: one dynamic binder edge in a hot loop; compare `_b` with the
// optional exact-guard hoist in a release build.
fn specialize(value: any as T) int => 1;
fn dynamic(value: any) int => specialize(value) or 0;
let seed = specialize(0);

pn benchmark() int {
    var total: int = 0
    var i: int = 0
    while (i < 1000000) {
        total = total + dynamic(i)
        i = i + 1
    }
    return total + seed - 1
}

pn main() {
    print(benchmark())
}

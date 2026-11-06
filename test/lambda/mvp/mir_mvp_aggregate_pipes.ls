// S10.1.2v4: whole-value pipes prepend one receiver, without walking it.
fn offset(value: int, delta: int) int { return value + delta }
fn default_offset(value: int, delta: int = 3) int { return value + delta }
fn difference(value, delta) { return value - delta }
fn join_pair(left: string, right: string) string { return left ++ right }
pn add_to(var value: int, delta: int) { value = value + delta }
pn observed(value: int) int { print("."); return value }
pn main() {
    print([[1, 2, 3] |> sum, "hé" |> len, 8 |> shr(1)]); print("\n")
    print(3 |> fill(7)); print("\n")
    print([1, 2, 3, 4] |> slice(1, 3)); print("\n")
    print([8 |> offset(5), 8 |> default_offset, 8 |> difference(3),
        4.5 |> difference(2)]); print("\n")
    print("left" |> join_pair("right")); print("\n")
    var current: int = 7
    current |> add_to(5)
    print(current); print("\n")
    let operations = [offset]
    print(8 |> operations[0](4)); print("\n")
    print([1, 2] |> (~ |> offset(3))); print("\n")
    let rejected = error("pipe receiver")
    print((rejected |> len) is error); print("\n")
    // S7.7.1v3 leaves strict order open; count evaluations without pinning it.
    let evaluated_once = observed(1) |> offset(observed(2))
    print(evaluated_once); print("\n")
    let dynamic_once = observed(1) |> operations[observed(0)](observed(2))
    print(dynamic_once); print("\n")
}

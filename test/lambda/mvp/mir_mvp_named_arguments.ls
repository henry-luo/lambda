// D7.2.2/S12.3.2: static named calls share formal positions with positional calls.
fn combine(first: int, second: int = 2, third: int = first + second) int {
    return first * 100 + second * 10 + third
}
fn subtract(left, right) { return left - right }
fn selected(values: int[], index: int) int? { return values[index] }
pn add_to(var value: int, delta: int) { value = value + delta }
fn opaque(value: any) any { return value }
fn checked(value: int, other: int) int { return value + other }
pn main() {
    print([combine(third: 9, first: 1), combine(second: 4, first: 3),
        combine(2, third: 8)]); print("\n")
    print([subtract(right: 2, left: 8), subtract(right: 2, left: 4.5)]); print("\n")
    print([selected(index: 1, values: [7, 8]), selected(index: 9, values: [7, 8])]); print("\n")
    var current: int = 5
    add_to(delta: 4, value: current)
    print(current); print("\n")
    print(3 |> combine(third: 8, second: 4)); print("\n")
    print([3 |> combine(third: 8), 3 |> combine(second: 4)]); print("\n")
    current |> add_to(delta: 2)
    print(current); print("\n")
    let rejected = checked(other: 2, value: opaque("bad"))
    print(rejected is error); print("\n")
}

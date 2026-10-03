// S7.7.2-S7.7.4/D3.3.3v3: exact arms preserve all dynamic boundary failures.
fn dynamic(value) any => value
pn integers(count, value) int[] {
    var values: int[] = fill(count, value)
    return values
}
pn floating(count, value) float[] {
    var values: float[] = fill(count, value)
    return values
}
pn booleans(count, value) bool[] {
    var values: bool[] = fill(count, value)
    return values
}
type One = 1
pn ones(value) One[] {
    var values: One[] = fill(2, value)
    return values
}
pn counted(count) int[2] {
    var values: int[2] = fill(count, 3)
    return values
}
// S11.4.6: declaration boundaries enforce constrained bases only.
type Positive = int that ~ > 0
type Negative = int that ~ < 0
type NonEmpty = int[] that len(~) > 0
pn positives(value) Positive[] {
    var values: Positive[] = fill(2, value)
    return values
}
pn negatives(values) Negative[] { return values }
pn nonempty(count) NonEmpty { return fill(count, 1) }
pn positive_store(value) Positive[] {
    var values: Positive[] = fill(2, 1)
    values[0] = value
    return values
}
pn invalid_scalar() int {
    var value: int = dynamic("wrong")
    return value
}
pn missing_local(values: int[]) int {
    var value: int = values[10]
    print("rejected declaration continued")
    return value
}
pn missing_assignment(values: int[]) int {
    var value: int = 1
    value = values[10]
    print("rejected assignment continued")
    return value
}
type Node = {value: float, label: string, next: Node?}
fn node(value, label, next) Node => {value: value, label: label, next: next}
fn read_node(value: Node?) => value.value
type Scalars = {n: int, b: bool, f: float, s: string}
fn read_n(value: Scalars?) => value.n
fn read_b(value: Scalars?) => value.b
fn read_f(value: Scalars?) => value.f
fn read_s(value: Scalars?) => value.s
fn add_int(a: int, b: int) int => a + b
fn sub_int(a: int, b: int) int => a - b
fn mul_int(a: int, b: int) int => a * b
pn defective_arithmetic() { return invalid_scalar() + 1 }
pn absent_arithmetic(values: int[]) { return values[9] + 1 }
pn main() {
    print([integers(0, 7), integers(2.0, 7), integers(2, 7.0),
        floating(2, 7), floating(2, 0.5)]) print("\n")
    print([integers(-1, 7) is error, integers(0.5, 7) is error,
        integers(null, 7) is error, integers(2, "wrong") is error,
        integers(9223372036854775807i64, 7) is error]) print("\n")
    print([ones(1), ones(2) is error, counted(2), counted(3) is error,
        invalid_scalar() is error]) print("\n")
    print([missing_local([1]) is error, missing_assignment([1]) is error]) print("\n")
    print([booleans(0, true), booleans(2, true), booleans(2, 1) is error,
        integers(0, (1, 2)) == [], integers(2, (1, 2)),
        integers(2, invalid_scalar()) is error]) print("\n")
    var values: int[] = [1, 2, 3]
    let snapshot = values
    values[0] = 9
    values[1] = dynamic(8.0)
    values[2] = (4, 5)
    print([values, snapshot]) print("\n")
    let first = node(2, "first", null)
    let second = node(3.5, "second", first)
    print([first.value is float, second.next.label, read_node(null),
        node("wrong", "label", null) is error,
        read_node({value: 6, label: "loose", next: null}),
        read_node({value: 7, label: "extra", next: null, extra: 9})]) print("\n")
    print([read_n(null), read_b(null), read_f(null), read_s(null)]) print("\n")
    print([positives(1), positives(-1) is error, negatives(positives(1)) is error,
        nonempty(2), nonempty(0) is error, positive_store(2),
        positive_store(-1) is error]) print("\n")
    print([add_int(7, 5), sub_int(7, 5), mul_int(7, 5),
        add_int(9007199254740991, 1), sub_int(-9007199254740991, 1),
        mul_int(9007199254740991, 2), mul_int(int(inf), 0),
        defective_arithmetic() is error, absent_arithmetic([1])]) print("\n")
}

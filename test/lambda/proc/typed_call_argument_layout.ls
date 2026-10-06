// S7.7.2-S7.7.4: constructing an argument layout preserves entry failure and order.
type Pair = {value: float, label: string}
type Nested = {child: Pair?, count: int}
type Nominal { value: int }
type Derived : Nominal { extra: string }
type IntRecord = {value: int}
type FloatRecord = {value: float}
type WideRecord = {value: i64}
fn dynamic(value) any => value
fn nominal(value: Nominal) int => value.value
fn nominal_identity(value: Nominal) any => value
fn nominal_plain() any { nominal(dynamic({value: 2})) }
fn choose_nominal(flag) any {
    let original = <Nominal value: 9>
    nominal(if (dynamic(flag)) original else {value: dynamic(2)})
}
fn piped_float(first: IntRecord, second: FloatRecord) float => second.value
fn piped_wide(first: FloatRecord, second: WideRecord) i64 => second.value
fn piped_many(first: IntRecord, a: string, b: string, second: FloatRecord) any {
    [first.value, a, b, second.value]
}
pn piped_borrow(var value: IntRecord, amount: int) int {
    value.value = value.value + amount
    return value.value
}
fn piped_tail(value: IntRecord, remaining: int) int {
    if (remaining == 0) value.value else value |> piped_tail(remaining - 1)
}
pn mark(var trace: array, value: int) int {
    push(trace, value)
    return value
}
pn accept(value: Pair, marker: int) any {
    return [value.value is float, value.value, value.label, marker]
}
pn nested(value: Nested) any {
    return [value.child.value is float, value.child.label, value.count]
}
pn reject(var trace: array) any {
    let result = accept({value: dynamic("wrong"), label: "bad"}, mark(trace, 5))
    print("rejected argument continued\n")
    return result
}
pn failed_fill(var trace: array) any {
    let result = take_array(fill(2, "wrong"), mark(trace, 6))
    print("rejected fill argument continued\n")
    return result
}
pn take_array(values: int[], marker: int) any { return [values, marker] }
pn main() {
    var trace = []
    print(accept({value: mark(trace, 1), label: "first"}, mark(trace, 2)))
    print("\n")
    print(accept(marker: mark(trace, 4), value: {value: mark(trace, 3), label: "named"}))
    print("\n")
    print(nested({child: {value: dynamic(7), label: "nested"}, count: 2}))
    print("\n")
    print(trace)
    print("\n")
    // S7.7.1 does not prescribe evaluation after a failed argument boundary.
    print([reject(trace) is error, failed_fill(trace) is error])
    print("\n")
    print([choose_nominal(true), choose_nominal(false) is error, nominal_plain() is error])
    print("\n")
    print(nominal_identity(<Derived value: 10, extra: "x">) is Derived)
    print("\n")
    // the virtual receiver must not donate its field lane to the explicit argument
    print({value: 1} |> piped_float({value: dynamic(2.5)}))
    print("\n")
    print({value: 1.0} |> piped_wide({value: dynamic(9007199254740993i64)}))
    print("\n")
    print({value: 3} |> piped_many("a", "b", {value: dynamic(4.5)}))
    print("\n")
    var borrowed: IntRecord = {value: 10}
    let snapshot = borrowed
    print(borrowed |> piped_borrow(2))
    print("\n")
    print([borrowed.value, snapshot.value, borrowed |> piped_tail(32)])
    print("\n")
    var named: IntRecord = {value: 10}
    let named_snapshot = named
    piped_borrow(amount: 3, value: named)
    print([named.value, named_snapshot.value])
    print("\n")
}

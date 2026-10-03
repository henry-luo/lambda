// S7.7.2-S7.7.4: constructing an argument layout preserves entry failure and order.
type Pair = {value: float, label: string}
type Nested = {child: Pair?, count: int}
type Nominal { value: int }
fn dynamic(value) any => value
fn nominal(value: Nominal) int => value.value
fn choose_nominal(flag) {
    let original = <Nominal value: 9>
    nominal(if (dynamic(flag)) original else {value: dynamic(2)})
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
    print([choose_nominal(true), choose_nominal(false) is error])
    print("\n")
}

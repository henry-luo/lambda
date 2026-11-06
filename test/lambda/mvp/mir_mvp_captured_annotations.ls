// S11.4.8v2/D4.1.4: annotations retain lexical selections after escape.
fn nested_selection(values: (number as T)[]) type => T
fn make(T: type) {
    type Alias = T
    type Record = {saved: Alias}
    fn selected(value: T) T => value
    fn mixed(local: number as U, value: T) => [U, type(value), value]
    fn optional(value: Alias = 1) Alias => value
    fn zero() T => 0
    fn failure() T {
        let retained = error("captured payload")
        return retained
    }
    fn values(input: Alias[]) Alias[] => input
    fn record(input: Record) Record => input
    fn local(input) any {
        let selected: Alias = input
        return selected
    }
    return {selected:selected, mixed:mixed, optional:optional, zero:zero, failure:failure,
        values:values, record:record, local:local}
}
let floating = make(float)
let integral = make(int)
let error_values = make(error)
let churn = [for (i in 1 to 100) {text:string(i)}]
let stored = floating.record({saved:1.0e-310})
let selected_error = error_values.failure();
[
    type(floating.selected(1)) == float,
    type(integral.selected(1.0)) == int,
    floating.selected("bad") is error,
    floating.mixed(1, 2) == [int, float, 2.0],
    integral.mixed(1.5, 2.0) == [float, int, 2],
    type(floating.optional()) == float,
    type(integral.optional()) == int,
    floating.optional(null) is error,
    type(floating.zero()) == float,
    selected_error is error,
    selected_error.message == "captured payload",
    floating.values([1, 2]) == [1.0, 2.0],
    type(floating.values([1, 2])[0]) == float,
    stored.saved == 1.0e-310,
    type(floating.local(1)) == float,
    floating.local("bad") is error,
    nested_selection([1, 2]) == int,
    nested_selection([1, 2.5]) == float,
    len(churn) == 100
]

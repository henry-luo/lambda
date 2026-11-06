// S11.4.8v2/D4.1.4: every invocation owns its selections and exported values.
fn identity(T: type, value: T) T => value
fn joined(left: number as T, value: T, right: number as T) T => value
fn nested_type(value: any as T) {
    let read = () => T
    let allocate = [for (i in 1 to 40) {text: string(i)}]
    return [read(), T, len(allocate)]
}
fn make(value: any as T) {
    let saved = T
    return () => [T, saved, value]
}
fn retained(value: any as T) T {
    let allocate = [for (i in 1 to 80) {value: float(i), text: string(i)}]
    return value
}
let reader = make(9007199254740993i64)
let allocate = [for (i in 1 to 80) {text: string(i)}]
let saved = reader()
let dynamic = {identity: identity};
[
    type(identity(float, 1)) == float,
    identity(int, 1.0) == 1,
    dynamic.identity(i64, 9007199254740993i64) == 9007199254740993i64,
    joined(1, 2.5, 3.5) == 2.5,
    type(joined(1, 2, 3.5)) == float,
    nested_type(1) == [int, int, 40],
    nested_type("text") == [string, string, 40],
    saved[0] == i64, saved[1] == i64, saved[2] == 9007199254740993i64,
    retained(9007199254740993i64) == 9007199254740993i64,
    retained(1.0e-310) == 1.0e-310,
    len(allocate) == 80
]

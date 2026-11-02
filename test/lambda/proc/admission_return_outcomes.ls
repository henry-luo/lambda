// S11.4.1v5/S7.5.4: a deferred return check contributes to its call's outcome.
fn opaque(value) => value
fn checked_array(value) float[] => opaque(value)
fn checked_scalar(value) int => opaque(value)
fn checked_bool(value) bool => opaque(value)
fn widened_plain(value: int) float => value
fn widened_union(value: int) float^ => value
fn mixed_union(flag: bool) float^ => if (flag) 7 else 7.5
pn propagated_union() any^ { return widened_union(7)^ }
pn mixed_explicit(flag: bool) float^ {
    if (flag) { return 7 }
    return 7.5
}
fn forwarded(value) float[]^ => checked_array(value)
pn checked_explicit(value) float[] { return opaque(value) }
pn checked_native(value: int) int^ {
    if (value < 0) { raise error("native return") }
    return value
}
pn returned_native(value: int) int^ { return checked_native(value) }
// Closed forward results are available without resolving defaults or binders.
fn sum_down(n: int) int => if (n <= 0) 0 else n + sum_down(n - 1)
fn braced_sum(n: int) int {
    if (n <= 0) 0 else { let next = n - 1; n + braced_sum(next) }
}
fn present(value) bool => value != null
fn mutual_left(flag: bool) Verdict => if (flag) mutual_right(false) else true
fn mutual_right(flag: bool) Verdict => if (flag) mutual_left(false) else false
type Verdict = bool
type Selected = int
fn dependent(value: number as Selected) Selected => value
// Empty literal evidence proves success without promising an untyped carrier.
pn empty_vector() int[] { return [] }
fn empty_flags() bool[] => []
pn fill_vector(var values: int[]) any { push(values, 3) }
pn fill_flags(var values: bool[]) any { push(values, true) }
type Ordered = {a: int, b: int}
fn preserved_fill(n: int) float[]^ => fill(n, 1)
pn checked_int_read(values: int[], index: int) int { return values[index] }
pn checked_float_read(values: float[], index: int) float { return values[index] }
pn checked_bool_read(values: bool[], index: int) bool { return values[index] }
// A failed optimized literal must reach the receiver before a native call.
pn sum_float_literal(values: float[]) float { return values[0] + values[1] }
pn rejected_float_literals(values: float[], index: int) {
    let called = sum_float_literal([values[index], values[index + 1]])
    let checked: float[] = [values[index], values[index + 1]];
    return [called is error, checked is error, "continued"]
}
pn main() {
    let absent_int = checked_int_read([7], 5)
    let absent_float = checked_float_read([7.5], 5)
    let absent_bool = checked_bool_read([true], 5)
    let scalar_member: float^ = 7
    let record_member: Ordered^ = {b: 20, a: 10}
    let filled_members = preserved_fill(2)^
    var vector = empty_vector()
    var flags = empty_flags()
    fill_vector(vector)^
    fill_flags(flags)^
    let inferred = checked_array("bad")
    let admitted: float[]^ = checked_array("bad")
    var changed: float[]^ = []
    changed = checked_array("bad")
    let scalar = checked_scalar("bad")
    let flag = checked_bool("bad")
    let explicit = checked_explicit("bad")
    let forward = forwarded("bad")
    let recovered = checked_array("bad") ^ { 42 }
    let good: float[]^ = checked_array([1.5]);
    [inferred is error, admitted is error, changed is error, scalar is error,
     flag is error, explicit is error, forward is error, recovered, good[0], "continued",
     sum_down(4), braced_sum(4), mutual_left(true), mutual_right(true),
     dependent(1.5) == 1.5, present([error("nested")]),
     type(widened_plain(7)) == float, type(widened_union(7)) == int, type(propagated_union()) == int,
     type(mixed_union(true)) == int, type(mixed_union(false)) == float,
     type(mixed_explicit(true)) == int, type(mixed_explicit(false)) == float, returned_native(-1) is error, returned_native(7), vector, flags, type(scalar_member^) == int,
     record_member^.a, type(filled_members[0]) == int, absent_int is error, absent_float is error, absent_bool is error, rejected_float_literals([1.0], 5)]
}

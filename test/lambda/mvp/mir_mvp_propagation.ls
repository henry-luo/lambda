// explicit discharge and stored validation outcomes share the native error ABI (S7.6.3v3, S7.7.2v2).
type Box = {value: int}
fn checked(x: any) int^ { let admitted: int = x; return admitted }
fn plus_one(x: any) int^ { return checked(x)^ + 1 }
fn fused(x: any) int^ {
    let outcome: int = x
    let admitted: int = outcome^
    return admitted + 1
}
fn text(x: any) string^ { let admitted: string = x; return admitted }
fn suffix(x: any) string^ { return text(x)^ ++ "!" }
fn only_error(x: error) error { return x }
pn accept_outcome(x: int^, var values: int[]) any {
    values[0] = values[0] + 1
    return x is error
}
pn map_outcome(x: any) any {
    let value: Box = {value: x}
    return [value is error, "tail"]
}
pn array_outcome(x: any) any {
    let values: int[] = fill(2, x)^
    return [values is error, "tail"]
}
pn negative_fill() any {
    let values: int[] = fill(-1, 7)^
    print("UNEXPECTED NEGATIVE FILL\n")
    return values
}
pn scalar_update(var values: int[]) int^ {
    var first: int = values[0]
    first = int((first * 3) % 19)^
    values[0] = first
    return first
}
pn float_update(var values: float[]) float^ {
    var first: float = values[0]
    first = float((first + 0.25) * 2.0)^
    values[0] = first
    return first
}
pn repeated_float(var values: float[]) any {
    var i: int = 0
    while (i < 3) { float_update(values)^; i = i + 1 }
}
pn float_snapshot(x: any) any {
    let admitted: float = x
    let saved = admitted
    let garbage = fill(512, 0)^
    return [saved, admitted is error]
}
pn outcomes(x: any) any {
    let admitted: int = x
    var changed: int = 3
    changed = x
    return [admitted is error, changed is error, "tail"]
}
pn write_then_fail(var values: int[], x: any) int^ {
    values[0] = 11
    return checked(x)^
}
pn propagated(var values: int[], x: any) int^ {
    let result: int = write_then_fail(values, x)^
    values[0] = 99
    return result
}
pn intervening(var values: int[], x: any) int^ {
    let outcome: int = x
    values[0] = 23
    let admitted: int = outcome^
    values[0] = 99
    return admitted
}
pn main() {
    print([checked(7), plus_one(7), checked("bad") is error, plus_one("bad") is error]); print("\n")
    print(outcomes(7)); print("\n"); print(outcomes("bad")); print("\n")
    var values: int[] = [1]
    let rejected = propagated(values, "bad")
    print([rejected is error, values[0]]); print("\n")
    print([suffix("ok"), suffix(7) is error]); print("\n")
    print(checked("bad") ^ { 42 }); print("\n")
    let failed = intervening(values, "bad")
    print([fused(7), fused("bad") is error, failed is error, values[0]]); print("\n")
    print(float_snapshot(0.25)); print("\n")
    print(float_snapshot("bad")[1]); print("\n")
    let accepted = accept_outcome(checked("bad"), values)
    let wrong: any = "bad"
    // S7.7.3: reject the original string before entry; interpreter currently
    // enters with the validation error instead, incrementing values twice.
    let rejected_argument = accept_outcome(wrong, values)
    let error_value: any = checked("bad")
    print([accepted, rejected_argument is error, values[0], only_error(error_value) is error]); print("\n")
    print(map_outcome(7)); print("\n"); print(map_outcome("bad")); print("\n")
    print(array_outcome(7)); print("\n"); print(array_outcome("bad")); print("\n")
    print(negative_fill() is error); print("\n")
    var updated: int[] = [7]
    let snapshot = updated
    print([scalar_update(updated), updated[0], snapshot[0]]); print("\n")
    var empty: int[] = []
    print(scalar_update(empty) is error); print("\n")
    var floats: float[] = [0.5]
    print([float_update(floats), floats[0]]); print("\n")
    let saved_floats = floats
    print([float_update(floats), floats[0], saved_floats[0]]); print("\n")
    var empty_floats: float[] = []
    print(float_update(empty_floats) is error); print("\n")
    var kernel_values: float[] = [0.5]
    repeated_float(kernel_values)^
    print(kernel_values[0]); print("\n")
    let kernel_snapshot = kernel_values
    repeated_float(kernel_values)^
    print([kernel_values[0], kernel_snapshot[0]]); print("\n")
    print(repeated_float(empty_floats) is error); print("\n")
}

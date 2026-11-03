// branch demand preserves selected truth and short-circuit effects (S3.4, S3.5).
// Full MIR currently loses loop-guard errors; interpreter/AUTO match this golden.
pn touch(var calls: int[], value: any) any { calls[0] = calls[0] + 1; return value }
pn both(var calls: int[], left: any, right: any) any {
    while (touch(calls, left) and touch(calls, right)) { return 1 }
}
pn either(var calls: int[], left: any, right: any) any {
    while (touch(calls, left) or touch(calls, right)) { return 1 }
}
pn negate(var calls: int[], value: any) any {
    while (not touch(calls, value)) { return 1 }
}
pn main() {
    var calls: int[] = [0]
    let fault = int("bad")
    print([both(calls, null, true), calls[0]]); print("\n")
    calls[0] = 0; print([both(calls, true, "text"), calls[0]]); print("\n")
    calls[0] = 0; print([both(calls, false, fault), calls[0]]); print("\n")
    calls[0] = 0; let right_error = both(calls, true, fault)
    print([match right_error { case error: true default: false }, calls[0]]); print("\n")
    calls[0] = 0; let left_error = both(calls, fault, true)
    print([match left_error { case error: true default: false }, calls[0]]); print("\n")
    calls[0] = 0; print([either(calls, true, fault), calls[0]]); print("\n")
    calls[0] = 0; print([either(calls, null, "text"), calls[0]]); print("\n")
    calls[0] = 0; print([either(calls, null, ""), calls[0]]); print("\n")
    calls[0] = 0; print([negate(calls, null), negate(calls, false), negate(calls, true), calls[0]]); print("\n")
    calls[0] = 0; let negated_error = negate(calls, fault)
    print([match negated_error { case error: true default: false }, calls[0]]); print("\n")
}

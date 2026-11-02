// S7.6.3v3/D2.8.3: explicit propagation owns a callable's early error lane.
fn fail() int^ => error("returned")
pn unary_after(value: any) {
    let carried = value^
    return 7
}
pn condition_after(value: any) {
    if ((value == 0)^) { return 1 }
    return 2
}
pn call_after() {
    let carried = fail()^
    return 7
}
pn builtin_after() {
    let carried = math.wfc(1u64, [], [], [1, 1, false, 1])^
    return 9
}
pn main() {
    let source = error("condition")
    let unary = unary_after(source)
    let condition = condition_after(source)
    let nested = call_after()
    let builtin = builtin_after();
    [unary is error, unary.message, condition is error, condition.message,
     nested is error, nested.message, builtin is error,
     unary_after(1), condition_after(0), condition_after(1)]
}

// S7.6.8/S7.6.9: one operand evaluation, lazy bodies, fresh chained outcomes.
type First = error that ~.message == "first"
type Second = error that ~.message == "second"
pn observed(var calls: int, fail: bool, value: any) any^ {
    calls = calls + 1
    if (fail) raise error("first")
    return value
}
pn mark(var bodies: int, value: any) any {
    bodies = bodies + 1
    return value
}
pn main() {
    var calls = 0
    var bodies = 0
    let a: any^ = observed(calls, true, null) ^First { mark(bodies, 11) }
    let b: any^ = observed(calls, false, 0) or^ { mark(bodies, 99) }
    let c: any^ = observed(calls, false, false) or^ { mark(bodies, false) }
    let d: any^ = observed(calls, true, null) ^Second { mark(bodies, 99) } ^First { mark(bodies, 22) }
    let e: any^ = observed(calls, true, null) ^First { error("second") } ^Second { mark(bodies, 33) }
    let nested = error("first") ^ { error("second") ^ { ^.message } ++ ^.message };
    [calls, bodies, a, b, c, d, e, nested]
}

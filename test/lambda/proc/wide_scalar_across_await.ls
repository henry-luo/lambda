// RVO4: wide scalars must survive a suspension boundary.
// A suspension is a re-homing barrier (D5.1.3), and under the companion-lane
// return convention (RV5) the async spill tracker must only ever observe
// resolved Items — never a pending lane-1 Item whose payload rides a register
// that dies at the next call. int64 and out-of-band (subnormal) doubles are the
// two wide representations that can be returned, so both cross the await here,
// as call results and as values derived from them.
fn wide_int() { 9007199254740993i64 }
fn wide_float() { 5.0e-320 }

pn child() {
    sleep(1)
    wide_int()
}

pn main() {
    let before_int = wide_int()
    let before_float = wide_float()
    let handle = start child()
    let after = wait(handle)^
    // values captured before the suspension must be unchanged after it, and
    // must still be usable as wide operands rather than as tag bits
    print([before_int, before_float, after] ++ "\n")
    print([before_int + 1, after + 1, before_int == after] ++ "\n")
    print([type(before_int), type(before_float), type(after)] ++ "\n")
}

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
// Method fields publish on ordinary return, propagation, and raise alike.
type Progress {
    count: int = 0,
    pn finish(value: any) int^ {
        count = count + 1
        let carried = value^
        return count
    },
    pn returned() int^ {
        count = count + 1
        return error("method returned")
    },
    pn raised() int^ {
        count = count + 1
        raise error("method raised")
    }
}
pn write_checked(var values: int[], index: int) any {
    values[index] = 7
}
pn u32_binding(value: int) {
    let checked: u32 = value
    return [checked is error, if (checked is error) null else int(checked)]
}
pn main() {
    let source = error("condition")
    let unary = unary_after(source)
    let condition = condition_after(source)
    let nested = call_after()
    let builtin = builtin_after()
    var progress = <Progress count: 0>
    let completed = progress.finish(7)
    let stopped = progress.finish(error("method propagated"))
    let stopped_count = progress.count
    let returned = progress.returned()
    let raised = progress.raised()
    var values: int[] = [1, 2]
    let rejected_store = write_checked(values, 4)
    let accepted_store = write_checked(values, 1);
    [unary is error, unary.message, condition is error, condition.message,
     nested is error, nested.message, builtin is error,
     unary_after(1), condition_after(0), condition_after(1),
     completed, stopped is error, stopped_count, returned is error, raised is error, progress.count,
     rejected_store is error, values, u32_binding(0), u32_binding(4294967295),
     u32_binding(-1), u32_binding(4294967296)]
}

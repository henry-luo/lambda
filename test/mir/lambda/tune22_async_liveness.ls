// D5.1.1v2/D8.1.1v9: only `before`, which is read after wait(), needs a
// named async-frame slot. The synchronous tail's new bindings stay local.
pn delayed() {
    sleep(0)
    return 4
}

pn main() {
    let before = 7
    let handle = start(delayed)
    let value = wait(handle)
    let plus_one = value + 1
    let plus_two = plus_one + 1
    print([before, value, plus_one, plus_two])
}

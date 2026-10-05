// RA1: `before`, read after wait(), stays in its register across the park;
// nothing is spilled to an async frame.
pn delayed() {
    sleep(0)^
    return 4
}

pn main() {
    let before = 7
    let handle = start(delayed)
    // `or` engages wait's channel (S7.5.1) without a `^` exit edge, whose
    // own unwind state would add frame traffic unrelated to named locals
    let value = wait(handle) or 0
    let plus_one = value + 1
    let plus_two = plus_one + 1
    print([before, value, plus_one, plus_two])
}

// Action C (revised by RA1): a wait suspends the task's own stack, so the
// procedure needs no async-frame stores and no pending-pair resolution.

pn delayed_value() {
    sleep(0)^
    return 7i64
}

pn main() {
    let handle = start(delayed_value)
    let value = wait(handle)^
    print(string(value) ++ "\n")
}

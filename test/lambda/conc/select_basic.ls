pn slow() {
    // An unfulfilled mailbox makes the first completion deterministic.
    return receive()^
}

pn fast() {
    return "fast"
}

pn main() {
    let slow_handle = start slow()
    let fast_handle = start fast()
    let chosen = select(slow_handle, fast_handle, timeout: 60000)^
    print(wait(chosen)^)
    cancel(slow_handle)
    let done^cancelled = wait(slow_handle)
}

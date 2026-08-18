pn child() {
    // The timeout must not cancel a task parked without a competing timer.
    return receive()^
}

pn main() {
    let handle = start child()
    var first = null
    wait(handle, timeout: 1) ^ { first = ^ } ~ { first = ~ }
    print(first is error)
    send(handle, 8)^
    var second = null
    wait(handle)^ { second = ^ } ~ { second = ~ }
    print(second)
}

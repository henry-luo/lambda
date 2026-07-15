pn child() {
    // The timeout must not cancel a task parked without a competing timer.
    return receive()^
}

pn main() {
    let handle = start child()
    let first^err = wait(handle, timeout: 1)
    print(^err)
    send(handle, 8)^
    print(wait(handle)^)
}

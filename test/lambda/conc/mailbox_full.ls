pn worker() {
    // Keep the mailbox owner parked while the bounded queue is filled.
    sleep(60000)^
}

pn main() {
    let handle = start worker()
    var index = 0
    while (index < 1024) {
        send(handle, index)^
        index = index + 1
    }
    let sent^err = send(handle, 1024)
    print(^err)
    cancel(handle)
    let done^cancelled = wait(handle)
}

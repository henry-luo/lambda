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
    var sent = null
    send(handle, 1024) ^ { sent = ^ } ~ { sent = ~ }
    print(sent is error)
    cancel(handle)
    var done = null
    wait(handle) ^ { done = ^ } ~ { done = ~ }
}

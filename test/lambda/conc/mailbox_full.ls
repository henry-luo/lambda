pn worker() {
    // Keep the mailbox owner parked while the bounded queue is filled.
    sleep(60000)^
}

pn main() {
    let handle = start(worker)
    var index = 0
    while (index < 1024) {
        send(handle, index)^
        index = index + 1
    }
    var sent = null
    { let __error_outcome_1 = send(handle, 1024) ^ { ^ }; if (__error_outcome_1 is error) { sent = __error_outcome_1 } else match __error_outcome_1 { case any { sent = ~ } } }
    print(sent is error)
    cancel(handle)
    var done = null
    { let __error_outcome_2 = wait(handle) ^ { ^ }; if (__error_outcome_2 is error) { done = __error_outcome_2 } else match __error_outcome_2 { case any { done = ~ } } }
}

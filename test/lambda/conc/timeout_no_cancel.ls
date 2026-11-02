pn child() {
    // The timeout must not cancel a task parked without a competing timer.
    return receive()^
}

pn main() {
    let handle = start(child)
    var first = null
    { let __error_outcome_1 = wait(handle, timeout: 1) ^ { ^ }; if (__error_outcome_1 is error) { first = __error_outcome_1 } else match __error_outcome_1 { case any { first = ~ } } }
    print(first is error)
    send(handle, 8)^
    var second = null
    { let __error_outcome_2 = wait(handle) ^ { ^ }; if (__error_outcome_2 is error) { second = __error_outcome_2 } else match __error_outcome_2 { case any { second = ~ } } }
    print(second)
}

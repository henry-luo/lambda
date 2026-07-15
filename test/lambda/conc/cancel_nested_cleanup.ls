pn grandchild() {
    // Mailbox parks keep nested cancellation independent of host timer stalls.
    receive()^
    print("late")
}

pn child() {
    start grandchild()
    receive()^
}

pn main() {
    let handle = start child()
    sleep(1)^
    cancel(handle)
    let value^err = wait(handle)
    print(^err)
    print(err.message)
}

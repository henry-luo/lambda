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
    let value = wait(handle) ^ { ^ }
    print(value is error)
    print(value.message)
}

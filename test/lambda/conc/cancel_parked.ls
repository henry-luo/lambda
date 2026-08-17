pn child() {
    // A mailbox park has no wall-clock race with cancellation under host load.
    receive()^
    print("late")
}

pn main() {
    let handle = start child()
    sleep(1)^
    cancel(handle)
    let value = wait(handle) ^ { ^ }
    print(value is error)
    print(value.message)
    cancel(handle)
}

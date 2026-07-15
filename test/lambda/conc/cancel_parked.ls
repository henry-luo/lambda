pn child() {
    // A mailbox park has no wall-clock race with cancellation under host load.
    receive()^
    print("late")
}

pn main() {
    let handle = start child()
    sleep(1)^
    cancel(handle)
    let value^err = wait(handle)
    print(^err)
    print(err.message)
    cancel(handle)
}

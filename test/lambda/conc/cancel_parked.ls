pn child() {
    // A mailbox park has no wall-clock race with cancellation under host load.
    receive()^
    print("late")
}

pn main() {
    let handle = start(child)
    sleep(1)^
    cancel(handle)
    var value = null
    wait(handle) ^ { value = ^ } ~ { value = ~ }
    print(value is error)
    print(value.message)
    cancel(handle)
}

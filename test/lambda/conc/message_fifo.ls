pn worker() {
    let a = receive()^
    let b = receive()^
    let c = receive()^
    return a ++ b ++ c
}

pn main() {
    let handle = start worker()
    send(handle, "a")^
    send(handle, "b")^
    send(handle, "c")^
    print(wait(handle)^)
}

pn child(parent) {
    send(parent, {handle: self()})^
    return 7
}
pn main() {
    let a = start(child, [self()])
    let b = start(child, [self()])
    let first = receive()^
    let second = receive()^
    let result = [a == a, a != b, a == first.handle or a == second.handle,
        b == first.handle or b == second.handle, {h: a} == {h: a}, a == {}]
    wait(a)^
    wait(b)^
    return result
}

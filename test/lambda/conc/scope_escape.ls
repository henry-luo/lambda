pn child() {
    sleep(1)^
    return 9
}
pn make_child() {
    let handle = start child()
    return handle
}

pn main() {
    let handle = make_child()
    print("escaped")
    print(wait(handle)^)
}

pn child() {
    sleep(1)^
    print("child")
}
pn branch() {
    if (true) {
        start child()
        print("inside")
        return 7
    }
    return 0
}

pn main() {
    print(branch())
}

pn child() {
    sleep(1)^
    print("child")
}

pn main() {
    if (true) {
        start child()
        print("inside")
    }
    print("after")
}

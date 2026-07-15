pn main() {
    var value = 1
    pn child() {
        print(value)
    }
    start child()
}

pn child() {
    print("late")
}
pn fail() int^error {
    start child()
    print("before")
    raise error("boom")
}

pn main() {
    var value = null
    fail() ^ { value = ^ } ~ { value = ~ }
    print(value is error)
    print(value.message)
}

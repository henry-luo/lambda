pn child() {
    print("late")
}
pn fail() int^error {
    start child()
    print("before")
    raise error("boom")
}

pn main() {
    let value = fail() ^ { ^ }
    print(value is error)
    print(value.message)
}

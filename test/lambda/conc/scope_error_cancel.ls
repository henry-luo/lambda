pn child() {
    print("late")
}
pn fail() int^error {
    start child()
    print("before")
    raise error("boom")
}

pn main() {
    let value^err = fail()
    print(^err)
    print(err.message)
}

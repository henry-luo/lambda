pn child() {
    print("late")
}

pn main() {
    let handle = start(child)
    cancel(handle)
    var value = null
    wait(handle) ^ { value = ^ } ~ { value = ~ }
    print(value is error)
    print(value.message)
}

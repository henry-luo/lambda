pn child() {
    print("late")
}

pn main() {
    let handle = start child()
    cancel(handle)
    let value = wait(handle) ^ { ^ }
    print(value is error)
    print(value.message)
}

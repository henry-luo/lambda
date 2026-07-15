pn child() {
    print("late")
}

pn main() {
    let handle = start child()
    cancel(handle)
    let value^err = wait(handle)
    print(^err)
    print(err.message)
}

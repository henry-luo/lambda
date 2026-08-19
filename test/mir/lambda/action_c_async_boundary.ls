// Action C: an async wait is a suspension/restore boundary. Any pending pair
// must be resolved before the async frame stores its live words.

pn delayed_value() {
    sleep(0)
    return 7i64
}

pn main() {
    let handle = start(delayed_value)
    let value = wait(handle)^
    print(string(value) ++ "\n")
}

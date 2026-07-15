pn leaf(value) {
    sleep(1)^
    return value
}

pn invoke(operation, value) {
    return operation(value)
}

pn main() {
    let operation = leaf
    print(invoke(operation, 19))
}

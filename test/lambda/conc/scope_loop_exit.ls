pn child(value) {
    sleep(1)^
    print(value)
}

pn main() {
    var index = 0
    while (index < 3) {
        index = index + 1
        start child(index)
        if (index == 1) { continue }
        if (index == 2) { break }
    }
    print("after")
}

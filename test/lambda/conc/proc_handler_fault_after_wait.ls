pn recurse(n) int^ {
    if (n == 0) {
        return 0
    }
    return 1 + recurse(n - 1)
}

pn fault_after_wait() int^ {
    sleep(1)
    return recurse(1000000)
}

pn main() {
    var caught = null
    fault_after_wait() ^ { caught = ^ }
    print(type(caught))
    print(caught.code)
}

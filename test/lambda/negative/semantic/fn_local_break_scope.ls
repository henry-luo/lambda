fn outer() {
    while (false) {
        fn invalid() { break }
        invalid()
    }
    return 0
}
outer()

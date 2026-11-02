pn main() {
    var value = {n: 0}
    fn invalid() { value.n = 1 }
    invalid()
}

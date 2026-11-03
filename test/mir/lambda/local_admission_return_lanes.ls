// S7.7.2v2/S7.7.4v2/D2.8.3: retained local failures do not complete a callable.
pn keeps_initializer(value) int {
    let checked: int = value
    return 7
}
pn keeps_assignment(value) int {
    var checked: int = 0
    checked = value
    return 9
}
pn returns_outcome(value) int^ {
    let checked: int = value
    return checked
}
pn propagates_outcome(value) int^ {
    let checked: int = value
    return checked^
}
pn main() {
    [keeps_initializer("bad"), keeps_assignment("bad"),
     keeps_initializer(3), keeps_assignment(3),
     returns_outcome("bad") is error, propagates_outcome("bad") is error,
     returns_outcome(3), propagates_outcome(3)]
}

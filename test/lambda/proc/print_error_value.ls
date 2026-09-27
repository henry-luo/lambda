// LR12-25 (S7.9.3): print participates in an error -- it renders the error
// instead of printing nothing, and a `++` chain over an error is that error.
fn check(x) {
    let n: int = x
    n + 1
}
pn main() {
    let r = check("abc")
    print("contained: ")
    print(r is error)
    print("\n[")
    print(r)
    print("]\n")
    print("x " ++ r ++ "\n")
    print("\n")
    print([1, r, 3])
    print("\n")
    print(check(4))
    print("\n")
}

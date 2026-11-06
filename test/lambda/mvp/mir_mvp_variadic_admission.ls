// S7.7.3/S16.9.7: rejected fixed arguments skip later rest operand effects.
pn later() {
    print("unexpected")
    return 9
}
pn guarded(n: int, invalid: any, ...) any {
    if (n == 0) { return 0 }
    return guarded(invalid, invalid, later())
}
pn main() {
    let rejected = guarded(1, "bad", "initial")
    return [rejected is error]
}

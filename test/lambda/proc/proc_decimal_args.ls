let GLOBAL_DEC = 3.14159N
let GLOBAL_FIXED_DEC = 1.25n

fn echo_decimal(d) => string(d)
fn add_decimal(a, b) => string(a + b)

pn print_decimal(label, d) {
    print(label)
    print("=")
    print(string(d))
    print("\n")
}

pn forward_decimal(label, d) {
    print_decimal(label, d)
}

pn main() {
    print_decimal("global-unlimited", GLOBAL_DEC)
    print_decimal("literal-unlimited", 2.71828N)
    let local_dec = 0.333N
    print_decimal("local-unlimited", local_dec)
    forward_decimal("forwarded-unlimited", GLOBAL_DEC)

    print_decimal("global-fixed", GLOBAL_FIXED_DEC)
    print_decimal("literal-fixed", 6.5n)
    let local_fixed = 4.75n
    forward_decimal("forwarded-fixed", local_fixed)

    print("fn-echo=")
    print(echo_decimal(GLOBAL_DEC))
    print("\n")
    print("fn-add=")
    print(add_decimal(GLOBAL_FIXED_DEC, 2.5n))
    print("\n")
}

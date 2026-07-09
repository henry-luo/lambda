let GLOBAL_DEC = 3.14159n
let GLOBAL_DEC_2 = 1.25n

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
    print_decimal("global-decimal", GLOBAL_DEC)
    print_decimal("literal-decimal", 2.71828n)
    let local_dec = 0.333n
    print_decimal("local-decimal", local_dec)
    forward_decimal("forwarded-decimal", GLOBAL_DEC)

    print_decimal("global-decimal-2", GLOBAL_DEC_2)
    print_decimal("literal-decimal-2", 6.5n)
    let local_fixed = 4.75n
    forward_decimal("forwarded-decimal-2", local_fixed)

    print("fn-echo=")
    print(echo_decimal(GLOBAL_DEC))
    print("\n")
    print("fn-add=")
    print(add_decimal(GLOBAL_DEC_2, 2.5n))
    print("\n")
}

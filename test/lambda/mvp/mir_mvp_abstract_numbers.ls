// S11.4.1v5/D2.3.1: abstract numeric contracts admit values, not Type* carriers.
fn opaque(value: any) any { return value }
fn number_identity(value: number) number { return value }
fn integer_identity(value: integer) integer { return value }
fn checked_number(value: any) number^ { return value }
fn checked_integer(value: any) integer^ { return value }
pn main() {
    print([number_identity(7) == 7, number_identity(3.25) == 3.25,
        integer_identity(9) == 9]); print("\n")
    let retained = number_identity(1e-310)
    let pressure = [number_identity(2.5), number_identity(4.5)]
    print([retained == 1e-310, pressure == [2.5, 4.5]]); print("\n")
    let deferred: number = opaque(5.5)
    print(deferred == 5.5); print("\n")
    let wrong_number = checked_number("text")
    let wrong_integer = checked_integer(3.5)
    print([wrong_number is error, wrong_integer is error]); print("\n")
    let functions = [number_identity]
    print(functions[0](6.25) == 6.25); print("\n")
}

// Exact dynamic numeric admission converts only at typed boundaries; runtime
// membership remains type-directional after the source value is preserved.
fn dynamic(value) any { value }
fn accept(value: int) int { value }
fn accept_float(value: float) float { value }
fn return_int(value) int { dynamic(value) }

type Person = {age: int}

pn main() {
    let declared: int = dynamic(3.0)
    let argument = accept(dynamic(4.0))
    let float_from_int = accept_float(dynamic(9))
    let static_float_from_int = accept_float(10)
    let returned = return_int(5.0)

    var reassigned: int = 0
    reassigned = dynamic(6.0)

    var person: Person = {age: 1}
    person.age = dynamic(7.0)

    var values: int[] = [1, 2]
    values[1] = dynamic(8.0)

    let decimal_value: decimal = dynamic(1.25)
    let signed_value: i8 = dynamic(-128.0)
    let unsigned_value: u8 = dynamic(255.0)
    let decimal_int: int = dynamic(3m)
    let signed64: i64 = dynamic(-9223372036854775808i64)
    let unsigned64: u64 = dynamic(18446744073709551615u64)
    let half_value: f16 = dynamic(1.5)
    let float_value: f32 = dynamic(16777216.0)
    let dynamic_match = match dynamic(3.0) {
        case int: true
        default: false
    }

    print(string([declared, argument, float_from_int, static_float_from_int, returned, reassigned, person.age, values[1],
        decimal_value is decimal, signed_value is i8, unsigned_value is u8,
        decimal_int is int, signed64 is i64, unsigned64 is u64, half_value is f16,
        float_value is f32, dynamic(3.0) is int, dynamic_match, declared is int,
        float_from_int is float, static_float_from_int is float, person.age is int, values[1] is int]) ++ "\n")
}

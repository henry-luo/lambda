// G6: a heterogeneous-union local keeps the boxed Item carrier from its
// declaration (its tag is the member record), so a member switch works in both
// directions, statically and dynamically; an out-of-union store is rejected at
// the assignment boundary; and `T | null` keeps its native nullable lane.

fn dynamic(value) any { value }

pn switch_bad() any^ {
    var a: int | string = 42
    a = dynamic(1.5)
    return a
}

pn main() {
    // static member switches, both directions
    var z: int | float = 1
    z = 1.5
    let after_float = z
    z = 2
    // dynamic member switch through an untyped producer
    var d: int | float = 1
    d = dynamic(2.5)
    // pointer member switch
    var a: int | string = 7
    a = "seven"
    // is-checks see the current member
    let z_is_int = z is int
    let d_is_float = d is float
    let a_is_string = a is string
    // T | null keeps the native nullable lane
    var x: int | null = 5
    x = null
    let x_null = x is null
    x = 9
    // out-of-union member is rejected, containable as an error
    var err = null
    switch_bad() ^ { err = ^ }
    print(string([after_float, z, d, a, z_is_int, d_is_float, a_is_string,
        x_null, x, err is error]) ++ "\n")
}

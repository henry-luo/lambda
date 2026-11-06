// S11.1.2v4/S11.4.6v4: type values and their members have distinct carriers;
// deferred admission still converts a later successful scalar store.
type Digits = \("\d"+), Words = \("\w"+)
fn opaque(x) => x
fn word(x: Words) => x
fn local_word(x) any^ {
    type LocalDigits = \("\d"+), LocalWords = \("\w"+)
    let result: LocalWords = x
    result
}
fn float_div(x: float, y: int) any^ => x div y
fn float_mod(x: float, y: int) any^ => x % y
fn mixed(flag: bool) => match (flag) { case true: 7 default: 7.0 }

pn main() {
    let inferred = (\("\w"+))
    let selected = [match (opaque(error("selected"))) { case error: true default: false },
                    match (opaque(1)) { case error: true default: false }]
    var scalar: float = 0.0
    scalar = opaque("bad")
    scalar = 7
    print(["abc" is inferred, word("abc") == "abc", local_word("xyz") == "xyz",
           selected == [true, false], type(scalar) == float, scalar == 7.0,
           float_div(7.0, 2) == 3.0, float_mod(7.0, 2) == 1.0,
           type(mixed(true)) == int, type(mixed(false)) == float])
    print("\n")
}

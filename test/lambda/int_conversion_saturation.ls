// S4.1.6/S1.6: numeric conversion always succeeds as int, saturating by sign.
fn dynamic(value) int^ => int(value)
fn to_int_float(value: float) int => int(value)
fn fixed(value: decimal) int => int(value)
fn optional_float(value: float?) int^ => int(value)
fn optional_int(value: int?) int^ => int(value)
fn inferred_optional(value: float?) {
    let converted = int(value);
    [converted is error, type(converted)]
}
let numeric = [to_int_float(1e20), to_int_float(-1e20), int(9007199254740992.0),
    int(-9007199254740992.0), dynamic(9223372036854775807i64),
    dynamic(-9223372036854775808i64), dynamic(18446744073709551615u64),
    int(9007199254740992.0m), fixed(1e100m), fixed(-1e100m)]
let finite = [to_int_float(2.9), to_int_float(-2.9), to_int_float(1e-310), to_int_float(-0.0),
    fixed(9007199254740991.9m), fixed(-9007199254740991.9m),
    dynamic(42i64), dynamic(42u64), int(2.75f32), int(-2.75f16)]
let specials = [to_int_float(inf) == inf, to_int_float(-inf) == -inf,
    to_int_float(nan) != to_int_float(nan),
    fixed(decimal.inf) == inf, fixed(-decimal.inf) == -inf,
    fixed(decimal.nan) != fixed(decimal.nan)]
let texts = [" +42 \t\r\n", '-42', "000000000000000000000000000001",
    "9007199254740991", "9007199254740992", "-9007199254740992",
    "9999999999999999999999999999999999999999999999999999999999999",
    "-9999999999999999999999999999999999999999999999999999999999999"]
let parsed = texts |> dynamic(~)
let invalid = ["3.7", "1e3", "0x10", "1_000", "nan", "inf", "-inf",
    "", " \t", "+", "--1", "12tail", "1 2", "42" ++ chr(0) ++ "tail"]
let rejected = invalid |> (dynamic(~) is error)
let original = error("conversion source")
let preserved = dynamic(original)
let absent = [optional_float(null) is error, optional_int(null) is error,
    inferred_optional(null), optional_float(2.9), dynamic(null) is error,
    dynamic(true) is error, dynamic([]) is error]
let result = [numeric, finite, specials, parsed,
    all((numeric ++ finite ++ parsed) |> (type(~) == int)), all(rejected),
    preserved is error, preserved.message == "conversion source", absent]
result

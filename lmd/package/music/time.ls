// Exact quarter-note fractions; bounded products stay inside the exact integer lane.
pub let ZERO = {num: 0, den: 1}
pub let ONE = {num: 1, den: 1}
let LIMIT = 1000000000
fn gcd(a, b) => if (b == 0) a else gcd(b, a % b)

pub fn ratio(n, d = 1) map^ {
    let checked = if (not (n is int) or not (d is int) or d == 0 or abs(n) > LIMIT or abs(d) > LIMIT)
        raise error("music time: invalid or excessive rational value")
    let g = gcd(abs(n), abs(d))
    {num: (n div g) * (if (d < 0) -1 else 1), den: abs(d) div g}
}

pub fn add(a, b) map^ {
    let g = gcd(a.den, b.den)
    ratio(a.num * (b.den div g) + b.num * (a.den div g), a.den * (b.den div g))^
}
pub fn neg(a) => {num: -a.num, den: a.den}
pub fn sub(a, b) map^ => add(a, neg(b))^
pub fn mul(a, b) map^ {
    let g = gcd(abs(a.num), b.den)
    let h = gcd(abs(b.num), a.den)
    ratio((a.num div g) * (b.num div h), (a.den div h) * (b.den div g))^
}
pub fn divide(a, b) map^ => mul(a, ratio(b.den, b.num)^)^
pub fn value(a) => float(a.num) / float(a.den)
pub fn compare(a, b) => sign(a.num * b.den - b.num * a.den)
pub fn later(a, b) => if (compare(a, b) >= 0) a else b

pub fn from_decimal(raw) map^ {
    let s = trim(string(raw))
    let negative = slice(s, 0, 1) == "-"
    let body = if (negative or slice(s, 0, 1) == "+") slice(s, 1, len(s)) else s
    let fields = split(body, ".")
    let digits = join(fields, "")
    let checked = if (len(fields) > 2 or len(digits) == 0 or len(digits) > 9 or
        any([for (c in split(digits, "")) ord(c) < 48 or ord(c) > 57]))
        raise error("music time: expected a bounded decimal number")
    let denominator = if (len(fields) == 2) int(10 ** len(fields[1])) else 1
    ratio(int(digits) * (if (negative) -1 else 1), denominator)^
}

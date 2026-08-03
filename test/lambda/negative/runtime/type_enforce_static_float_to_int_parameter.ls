// C16: `int` is the float64-representable integers, a SUBSET of float, and
// admission into it is decided by MEMBERSHIP -- is this value integral and
// finite -- not by the static type. So a float that IS an integer is admitted
// (`accept_int(3.0)` binds 3), while one that is not is rejected here.
//
// This fixture used to pass `3.0` and expect rejection, under the earlier rule
// that a statically-known float never receives implicit admission. C16 replaced
// that rule, and the literal convention depends on it: `1e2` is a float
// literal, so `let n: int = 1e2` must bind 100.
fn accept_int(value: int) int { value }

pn main() {
    accept_int(3.5)
}

// S7.4.2, D2.4.1–D2.4.3: a parameter-error guard must preserve the
// boxed value/error join until propagation checks it, including native returns.
fn positive(x) float^ {
    if (not (x is number) or x <= 0 or x == inf or x is nan)
        raise error("positive number required")
    else float(x)^
}
fn checked_int(x) int^ {
    if (x is int and x > 0) x else raise error("positive int required")
}
fn checked_bool(x) bool^ {
    if (x is bool) x else raise error("bool required")
}
fn checked_string(x) string^ {
    if (x is string) x else raise error("string required")
}
fn plain(row) map^ => {v: positive(row.v)^}
fn spread(row) map^ => {*:row, v: positive(row.v)^}
fn collected(rows) array^ => [for (row in rows) {*:row, v: positive(row.v)^}]
fn indexed(xs) float^ => positive(xs[0])^
fn ints(row) int^ => checked_int(row.v)^
fn bools(row) bool^ => checked_bool(row.v)^
fn strings(row) string^ => checked_string(row.v)^;
[
    (plain({v: 0}) ^ {null}) == null,
    (spread({v: "0"}) ^ {null}) == null,
    (collected([{v: 1}, {v: inf}]) ^ {null}) == null,
    (indexed([nan]) ^ {null}) == null,
    (ints({v: -1}) ^ {null}) == null,
    (bools({v: 1}) ^ {null}) == null,
    (strings({v: false}) ^ {null}) == null,
    (positive({v: 0}.v) ^ {null}) == null,
    (positive({v: error("argument")}.v) ^ {null}) == null,
    (plain({v: 2}) ^ {null}) == {v: 2.0},
    (spread({v: 3, other: 4}) ^ {null}) == {v: 3.0, other: 4},
    (collected([{v: 1}, {v: 2}]) ^ {null}) == [{v: 1.0}, {v: 2.0}],
    (indexed([4]) ^ {null}) == 4.0,
    (ints({v: 7}) ^ {null}) == 7,
    (bools({v: false}) ^ {null}) == false,
    (strings({v: "ok"}) ^ {null}) == "ok"
]

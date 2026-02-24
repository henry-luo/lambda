// Test is/in operator precedence with and/or
// is/in should bind tighter than and/or:
//   x is T and y  →  (x is T) and y
//   x in arr or y →  (x in arr) or y

// ---- is ... and ----

// is element and boolean_expr
fn check_el(x) {
    x is element and (len(x) > 0)
}

check_el(<div; "a">)       // true: is element=true, len>0=true
check_el(<div>)             // false: is element=true, len>0=false
check_el("hello")           // false: is element=false

// is string and boolean_expr
fn check_str(x) {
    x is string and (len(x) > 3)
}

check_str("hello")          // true
check_str("hi")             // false: len<=3
check_str(42)               // false: not string

// is int and comparison
fn check_int(x) {
    x is int and (x > 10)
}

check_int(42)               // true
check_int(3)                // false: x<=10

// ---- is ... or ----

fn check_is_or(x) {
    x is string or x is int
}

check_is_or("hi")           // true
check_is_or(42)             // true
check_is_or(null)           // false

// ---- in ... and ----

fn check_in_and(x, arr) {
    x in arr and (x > 2)
}

check_in_and(3, [1, 2, 3])  // true: in arr=true, >2=true
check_in_and(1, [1, 2, 3])  // false: in arr=true, >2=false
check_in_and(5, [1, 2, 3])  // false: not in arr

// ---- in ... or ----

fn check_in_or(x) {
    x in [1, 2] or x in [3, 4]
}

check_in_or(1)              // true
check_in_or(3)              // true
check_in_or(5)              // false

// ---- combined: is and is ----

fn check_both(x, y) {
    x is string and y is int
}

check_both("hi", 42)        // true
check_both("hi", "no")      // false
check_both(42, 42)          // false

// ---- compound: is ... and ... and ... ----

fn check_compound(x) {
    x is string and (len(x) > 2) and (len(x) < 6)
}

check_compound("hello")     // true: string, len=5, 2<5<6
check_compound("hi")        // false: len=2, not >2
check_compound("toolong!")   // false: len=8, not <6
check_compound(42)           // false: not string

// ---- not is ... and ----

fn check_not_is(x) {
    not (x is int) and x is string
}

check_not_is("hi")          // true
check_not_is(42)            // false

// ---- is with == ----

fn check_is_eq(x) {
    let n = name(x)
    x is element and n == 'div'
}

check_is_eq(<div; "a">)    // true
check_is_eq(<span>)        // false: name != div
check_is_eq("text")        // false: not element

// ---- in with == ----

fn check_in_eq(x) {
    x in [1, 2, 3] and x == 2
}

check_in_eq(2)              // true
check_in_eq(1)              // false: x != 2
check_in_eq(5)              // false: not in arr

// ---- combined summary ----
[
    check_el(<div; "a">),       // true
    check_str("hello"),         // true
    check_int(42),              // true
    check_is_or("hi"),          // true
    check_in_and(3, [1, 2, 3]), // true
    check_in_or(1),             // true
    check_both("hi", 42),       // true
    check_compound("hello"),    // true
    check_not_is("hi"),         // true
    check_is_eq(<div; "a">),    // true
    check_in_eq(2)              // true
]

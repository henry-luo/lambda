// Object type with field-level and object-level constraints

// === Field-level constraints ===
type User {
    name: string that (len(~) > 0),
    age: int that (0 <= ~ and ~ <= 150),
    email: string;
}

// valid user passes all constraints
let alice = {User name: "Alice", age: 30, email: "a@x.com"}
alice is User

// invalid: empty name violates string constraint
let bad_name = {User name: "", age: 30, email: "b@x.com"}
bad_name is User

// invalid: negative age violates int constraint
let bad_age = {User name: "Bob", age: -5, email: "b@x.com"}
bad_age is User

// invalid: age exceeds 150
let bad_age2 = {User name: "Carol", age: 200, email: "c@x.com"}
bad_age2 is User

// === Object-level constraints ===
type DateRange {
    start: int,
    end: int;
    that (~.end > ~.start)
}

let valid_range = {DateRange start: 1, end: 10}
valid_range is DateRange

let invalid_range = {DateRange start: 10, end: 1}
invalid_range is DateRange

// === Combined field + object constraints ===
type Config {
    min: int that (~ >= 0),
    max: int that (~ >= 0);
    that (~.max > ~.min)
}

// all constraints pass
let good_config = {Config min: 1, max: 10}
good_config is Config

// field constraint fails (negative min)
let bad_field = {Config min: -1, max: 10}
bad_field is Config

// object constraint fails (max <= min)
let bad_obj = {Config min: 5, max: 3}
bad_obj is Config

// type checking with unconstrained type still works
type Point { x: int, y: int }
let p = {Point x: 3, y: 4}
p is Point

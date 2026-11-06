// S11.4.8v2/S11.4.3: a selected error-admitting type keeps Error as data.
fn observe(T: type, value: T) bool => value is error
fn selected(value: any as T) type => T
fn joined(first: any as T, second: any as T) type => T
fn join_unknown(left: any, right: any) {
    let result = joined(left, right)
    return result is error
}
let failure = error("retained")
let first = observe(any, failure)
let second = observe(error, failure)
let selected_error = selected(failure)
let incompatible_join = join_unknown(1, failure);
[first == true, second == true, selected_error == error, incompatible_join == true]

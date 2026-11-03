// S7.7.2v2/S9.1.4v2: a deferred error remains a value in a shared local home.
fn retains_error(value) bool {
    var local_value: int = value
    pn replace() { local_value = 5 }
    return local_value is error
}

// S11.4.6v4: shared local homes retain the admitted normal representation.
fn admits_normal(value) bool {
    var local_value: float^ = value
    pn replace() { local_value = 5 }
    return local_value is float
}

[retains_error("bad"), not retains_error(7), admits_normal(7)]

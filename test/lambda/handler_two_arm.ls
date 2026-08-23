// S7.6.1v3/S7.6.2v3: the optional `~` arm receives every non-error value.
fn numeric_outcome(value, fail) int^ {
    if (fail) raise error("boom")
    else value
}

fn record_outcome(fail) any^ {
    if (fail) raise error("record failure")
    else {name: "ok"}
}

let success = numeric_outcome(21, false) ^ {
    ["error", ^.message]
} ~ {
    ["value", ~ * 2]
}

let failure = numeric_outcome(21, true) ^ {
    ["error", ^.message]
} ~ {
    ["value", ~ * 2]
}

let soft_error = error("soft") ^ {
    ^.message
} ~ {
    "unexpected"
}

let null_value = null ^ {
    "unexpected"
} ~ {
    if (~ is null) "null" else "not null"
}

let false_value = false ^ {
    "unexpected"
} ~ {
    if (~ is false) "false" else "not false"
}

let handled_member = record_outcome(false) ^ {
    {name: "error"}
} ~ {
    ~
}.name

// The error arm retains an enclosing current value; the value arm shadows it.
let outer_error_value = match 9 {
    case int: numeric_outcome(1, true) ^ { ~ } ~ { 0 }
}
let shadowed_value = match 9 {
    case int: numeric_outcome(21, false) ^ { 0 } ~ { ~ * 2 }
}

// Nested handlers restore the enclosing error/value bindings around each arm.
let nested_outer_error = numeric_outcome(1, true) ^ {
    numeric_outcome(2, false) ^ { "unexpected" } ~ { ^.message }
} ~ {
    "unexpected"
}
let nested_outer_value = numeric_outcome(5, false) ^ {
    "unexpected"
} ~ {
    numeric_outcome(1, true) ^ { ~ } ~ { 0 }
}

// An error produced by a selected arm is not consumed again by the same handler.
let error_arm_result = numeric_outcome(1, true) ^ {
    error("error arm")
} ~ {
    0
}
let value_arm_result = numeric_outcome(1, false) ^ {
    0
} ~ {
    error("value arm")
}

// Explicit value handling replaces the operand success type with the arm union.
let transformed: string = numeric_outcome(1, false) ^ {
    "error"
} ~ {
    "value"
};

[success, failure, soft_error, null_value, false_value, handled_member,
    outer_error_value, shadowed_value, nested_outer_error, nested_outer_value,
    error_arm_result is error, value_arm_result is error, transformed]

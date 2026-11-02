// S7.6.1v3/S7.6.2v3: the optional `~` arm receives every non-error value.
fn numeric_outcome(value, fail) int^ {
    if (fail) raise error("boom")
    else value
}

fn record_outcome(fail) any^ {
    if (fail) raise error("record failure")
    else {name: "ok"}
}

let success = { let __error_outcome_1 = numeric_outcome(21, false) ^ { ^ }; if (__error_outcome_1 is error) {
    ["error", __error_outcome_1.message]
} else match __error_outcome_1 { case any {
    ["value", ~ * 2]
} } }

let failure = { let __error_outcome_2 = numeric_outcome(21, true) ^ { ^ }; if (__error_outcome_2 is error) {
    ["error", __error_outcome_2.message]
} else match __error_outcome_2 { case any {
    ["value", ~ * 2]
} } }

let soft_error = { let __error_outcome_3 = error("soft") ^ { ^ }; if (__error_outcome_3 is error) {
    __error_outcome_3.message
} else match __error_outcome_3 { case any {
    "unexpected"
} } }

let null_value = { let __error_outcome_4 = null ^ { ^ }; if (__error_outcome_4 is error) {
    "unexpected"
} else match __error_outcome_4 { case any {
    if (~ is null) "null" else "not null"
} } }

let false_value = { let __error_outcome_5 = false ^ { ^ }; if (__error_outcome_5 is error) {
    "unexpected"
} else match __error_outcome_5 { case any {
    if (~ is false) "false" else "not false"
} } }

let handled_member = record_outcome(false) ^ {
    {name: "error"}
}.name

// The error arm retains an enclosing current value; the value arm shadows it.
let outer_error_value = match 9 {
    case int: { let __error_outcome_6 = numeric_outcome(1, true) ^ { ^ }; if (__error_outcome_6 is error) { ~ } else match __error_outcome_6 { case any { 0 } } }
}
let shadowed_value = match 9 {
    case int: { let __error_outcome_7 = numeric_outcome(21, false) ^ { ^ }; if (__error_outcome_7 is error) { 0 } else match __error_outcome_7 { case any { ~ * 2 } } }
}

// Nested handlers restore the enclosing error/value bindings around each arm.
let nested_outer_error = { let __error_outcome_8 = numeric_outcome(1, true) ^ { ^ }; if (__error_outcome_8 is error) {
    { let __error_outcome_9 = numeric_outcome(2, false) ^ { ^ }; if (__error_outcome_9 is error) { "unexpected" } else match __error_outcome_9 { case any { __error_outcome_8.message } } }
} else match __error_outcome_8 { case any {
    "unexpected"
} } }
let nested_outer_value = { let __error_outcome_10 = numeric_outcome(5, false) ^ { ^ }; if (__error_outcome_10 is error) {
    "unexpected"
} else match __error_outcome_10 { case any {
    { let __error_outcome_11 = numeric_outcome(1, true) ^ { ^ }; if (__error_outcome_11 is error) { ~ } else match __error_outcome_11 { case any { 0 } } }
} } }

// An error produced by a selected arm is not consumed again by the same handler.
let error_arm_result = { let __error_outcome_12 = numeric_outcome(1, true) ^ { ^ }; if (__error_outcome_12 is error) {
    error("error arm")
} else match __error_outcome_12 { case any {
    0
} } }
let value_arm_result = { let __error_outcome_13 = numeric_outcome(1, false) ^ { ^ }; if (__error_outcome_13 is error) {
    0
} else match __error_outcome_13 { case any {
    error("value arm")
} } }

// Explicit value handling replaces the operand success type with the arm union.
let transformed: string = { let __error_outcome_14 = numeric_outcome(1, false) ^ { ^ }; if (__error_outcome_14 is error) {
    "error"
} else match __error_outcome_14 { case any {
    "value"
} } };

[success, failure, soft_error, null_value, false_value, handled_member,
    outer_error_value, shadowed_value, nested_outer_error, nested_outer_value,
    error_arm_result is error, value_arm_result is error, transformed]

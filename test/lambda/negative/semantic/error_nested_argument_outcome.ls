// S7.7.3/S11.4.3: a nested rejected operand remains the outer call outcome.
fn failed() any^ => error("nested")
fn checked(value: int) int => value
let retained = checked(len(failed()))
let narrowed: int = retained
narrowed

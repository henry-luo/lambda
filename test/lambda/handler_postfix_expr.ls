// Postfix handlers bind at the member-access tier and require a primary operand.
fn fail() int^ {
    raise error("handler-prefix")
}

// a soft producer: its error flows through `+` to the handler as a value, while
// a raised `fail()` there would be unengaged at its immediate expression (S7.5.1)
fn soft_fail() int | error { error("handler-prefix") }

let handled_call = fail() ^ { "recovered" }
let handled_binary = (soft_fail() + 2) ^ { "recovered-binary" }
let handled_precedence = 1 + fail() ^ { 2 };
[handled_call, handled_binary, handled_precedence]

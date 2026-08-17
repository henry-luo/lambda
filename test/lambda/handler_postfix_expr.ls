// Postfix handlers bind at the member-access tier and require a primary operand.
fn fail() int^ {
    raise error("handler-prefix")
}

let handled_call = fail() ^ { "recovered" }
let handled_binary = (fail() + 2) ^ { "recovered-binary" }
let handled_precedence = 1 + fail() ^ { 2 }
[handled_call, handled_binary, handled_precedence]

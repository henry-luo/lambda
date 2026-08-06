// Prefix handler shorthand must accept both a call operand and a larger binary expression.
fn fail() int^ {
    raise error("handler-prefix")
}

let handled_call = ^ { "recovered" } fail()
let handled_binary = ^ { "recovered-binary" } fail() + 2
[handled_call, handled_binary]

// LR10-7 / S7.4.4: an error value owns its code and message.
let a = error("A")
let b = error("B")
let owned = [a.code, a.message, b.code, b.message]
owned

fn fail(m) int^ { raise error(m) }
let c = fail("C") ^ { ^ }
let d = error("D")
let raised = [c.message, d.message, (fail("E") ^ { ^.message })]
raised

let e = error({code: 304, message: "division by zero"})
let g = error({message: "message only"})
let mapped = [e.code, e.message, g.code, g.message, e is error]
mapped

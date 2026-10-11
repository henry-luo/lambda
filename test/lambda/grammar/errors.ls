// GM5.2/GM12: a failed parse raises like every input parser, with the
// furthest position reached and what was expected there.
grammar calc {
    expr   = prec.left(1, expr, "+", expr) | prec.left(2, expr, "*", expr) | primary
    primary = number | "(", expr, ")"
    space  := "\s"+
    number := "\d"+
}

let deep = join(fill(3000, "("), "") ++ "1" ++ join(fill(3000, ")"), "")
let nested = join(fill(200, "("), "") ++ "1" ++ join(fill(200, ")"), "")
let messages = [
    parse("1 + (2 * 3", calc) ^ { ^.message },
    parse("1 + 2 )", calc) ^ { ^.message },
    parse("1 +", calc) ^ { ^.message },
    parse("1\n+ x", calc) ^ { ^.message },
    parse("", calc) ^ { ^.message },
    parse("1", calc, 'nope') ^ { ^.message },
    parse(12, calc) ^ { ^.message },
    parse("1", calc.primary, 'number') ^ { ^.message },
    parse(deep, calc) ^ { ^.message }
]
for (m in messages) m ++ "\n"
parse(nested, calc)^ is element

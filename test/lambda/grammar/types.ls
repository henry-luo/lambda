// GM5v2: a grammar is a type and so is each rule; a public grammar crosses
// modules like a public type, including a read inside the defining module's
// own function.
import .mod_arith

grammar calc {
    expr   = prec.left(1, expr, "+", expr) | number
    space  := " "*
    number := "\d"+
}
calc
type(calc)
calc.expr
type(calc.expr)
calc.number
calc.missing == null
arith
parse("1 + 2 + 3", arith)^
sum_tree("4 + 5")

fn digits(text) {
    grammar d { n := "\d"+ }
    parse(text, d)^
}
digits("123")

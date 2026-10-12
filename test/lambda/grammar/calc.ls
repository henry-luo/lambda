// GM1/GM6: the design's calculator (vibe/Lambda_Design_Grammar_Parser.md §1),
// pinning the §5.2 precedence traces, unary and right-associative operators,
// the entry-rule forms of parse (GM5.1) and membership (GM5.3).
grammar calc {
    expr   = prec.left(1, expr, "+", expr)
           | prec.left(1, expr, "-", expr)
           | prec.left(2, expr, "*", expr)
           | prec.left(2, expr, "/", expr)
           | prec(3, "-", expr)
           | prec.right(4, expr, "^", expr)
           | primary
    primary = number | ident | "(", expr, ")"
    space  := "\s"+
    number := "\d"+ ("." "\d"+)?
    ident  := "\a" "\w"*
}

// a + (b * c)
parse("a + b * c", calc)^
// (a * b) + c
parse("a * b + c", calc)^
// (-a) * b
parse("-a * b", calc)^
// -(a ^ b)
parse("-a ^ b", calc)^
// a ^ (b ^ c)
parse("a ^ b ^ c", calc)^
// ((1 - 2) - 3)
parse("1 - 2 - 3", calc)^
// the first rule is the entry; a named rule overrides it
parse("(1.5)", calc, 'primary')^
// a `:=` rule as the entry yields its text
parse("42", calc, 'number')^
// a rule type names its own entry
parse("x*y", calc.primary) ^ { "primary rejects: " ++ ^.message }
parse("x", calc.primary)^
;["1 + 2" is calc, "1 +" is calc, 12 is calc, "" is calc]

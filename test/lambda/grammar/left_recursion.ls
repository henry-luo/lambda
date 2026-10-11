// GM6–GM8: direct left recursion with and without prec — postfix calls and
// indexes, a ternary with a middle operand, an unannotated chain (left
// associative), and a non-associative comparison that refuses to chain.
grammar post {
    expr = prec(10, expr, "(", args?, ")")
         | prec(10, expr, "[", expr, "]")
         | prec.right(1, expr, "?", expr, ":", expr)
         | prec.left(2, expr, "+", expr)
         | atom
    args = expr, ("," , expr)*
    atom := "\a"+ | "\d"+
    space := " "*
}
parse("f(a)(b, c)[0]", post)^
parse("a ? b ? c : d : e", post)^
parse("a ? b : c + d", post)^

// an unannotated chain is left-associative; its `term` text sits next to the
// "+" literal, and adjacent strings in element content merge (S2.6.4)
grammar chain {
    sum  = sum, "+", term | term
    term := "\d"+
    space := " "*
}
parse("1 + 2 + 3", chain)^

grammar cmp {
    rel  = prec.left(1, rel, "and", rel) | prec(2, rel, "<", rel) | num
    num  := "\d"+
    space := " "*
}
parse("1 < 2 and 2 < 3", cmp)^
parse("1 < 2 < 3", cmp) ^ { ^.message }

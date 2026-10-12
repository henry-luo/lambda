// GM9: every lint warning, none of which fails the compile
grammar lint {
    start = expr | cmp | cap | opt | rep | two
    expr  = prec.left(1, expr, "+", expr) | prec(2, expr, "<", expr) | expr, "*", expr | atom
    atom  = op | num | "(", expr, ")"
    op    = "<" | "<=" | "<<"
    cmp   = "<", num | "<=" , num
    cap   = "<" | "<=", num
    opt   = num? | "x"
    rep   = (num?)*
    two   = pair, "!" | pair, "?"
    pair  = num, num
    unused = "u"
    num   := "\d"+
}
grammar spaced {
    space := " "+
    x = "a"
}
"compiled"

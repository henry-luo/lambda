// §4.5 composition: `import g` brings g's rules in; a local rule of the same
// name overrides it, and the imported rules then call the override. An import
// written before every local rule supplies the entry (GM5.1).
import lambda.grammar.xpath
grammar base {
    list   = item, (",", item)*
    item   = word | number
    word   := "\a"+
    number := "\d"+
    space  := " "*
}
grammar ext {
    import base
    item   = word | number | quoted
    quoted = "<", word, ">"
}
grammar only {
    import base
}
grammar local_first {
    pair = word, "=", item
    import base
}
// a local `:=` island names imported `:=` rules: the local override when there
// is one, wherever the import is written; a later import overrides an earlier one
grammar tagged {
    import base
    word   := "\a" ("\a" | "-")*
    item   = tag | word | number
    tag    := word ":" number
}
grammar tag_first {
    item   = tag | number
    tag    := word "#" number
    import base
}
grammar two_imports {
    import base
    import tagged
    item   = pair | number
    pair   := word "=" word
}
// a grammar from another module: overrides and token references resolve by name
grammar xq {
    import xpath
    IntegerLiteral = integer, _n
    _n := "n"
    ParenthesizedExpr = ExtensionExpr | _lparen, _Sequence?, _rparen
    ExtensionExpr = Pragma, body: EnclosedExpr
    Pragma = _pragma_open, name: qname, _pragma_close
    qname := ncname ":" ncname
    _pragma_open := "(#"
    _pragma_close := "#)"
}
parse("a, 1, b", base)^
parse("a, <q>, 2", ext)^
parse("x, 9", only)^
parse("k = 3", local_first)^
"a, <q>" is base
"a, <q>" is ext
parse("ab-c:1, 2", tagged)^
parse("x#1", tag_first)^
parse("a-b=c, 4", two_imports)^
parse("(# ns:p #) {1n + 2n}", xq)^
parse("(1n, 2.5)", xq)^

// GM11: the third argument may be an options map; spans are byte offsets
parse("a, 1", base, {rule: 'list', spans: true})^
parse("  a ", base, {spans: true})^
parse("7", base, {rule: 'number'})^
parse("a", base, {spans: 1}) ^ { ^.message }

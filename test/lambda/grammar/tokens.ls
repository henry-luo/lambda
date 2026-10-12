// GM4.1/GM4.2: a `'...'` token rule emits symbols and a `"..."` one strings;
// literals match whole words; negative lookahead excludes keywords; `memo`.
grammar kw {
    stmt    = for, name: id, in, id | let, id, "=", id
    id      = !keyword, word
    keyword = for | in | let
    for     := 'for'
    in      := 'in'
    let     := 'let'
    word    := "\a" "\w"*
    space   := "\s"+
}

let t = parse("for x in xs", kw)^
t
;[for (c in t) type(c)]
t.name
// a keyword prefix is still an identifier
parse("for format in fork", kw)^
// a keyword is not an identifier
"for for in x" is kw
parse("let a = b", kw)^

grammar words {
    pair  = "go", word | "go"
    word  := "\a"+
    space := " "*
}
// "go" does not match the start of "gone": literals match whole words
parse("gone", words) ^ { ^.message }
parse("go on", words)^

grammar lists {
    list = item, item, word | item, word
    memo item = word, ","
    word := "\a"+
    space := " "*
}
parse("a, b, c", lists)^
parse("a, b", lists)^

// GM4.3: a quoted symbol in a rule body is a name — a rule, or a label
grammar quoted {
    pair   = 'key', "=", 'data-value': key
    key    := "\a"+
    space  := " "*
}
parse("a = b", quoted)^

// GM13: `~x` matches x where the previous token ended, with no space skipped
// first (Tree-sitter's token.immediate), so text after `}` keeps its space,
// while the expression inside the braces skips space as usual
grammar tmpl {
    Text  = _q, (~chunk | Hole)*, ~_q
    Hole  = ~"{", Sum, "}"
    Sum   = prec.left(1, left: Sum, op: plus, right: Sum) | num
    chunk := !("\"" | "{")+
    num   := "\d"+
    plus  := '+'
    _q    := "\""
    space := " "*
}
parse("\"a { 1 + 2 } b\"", tmpl)^
parse("\" lead\"", tmpl)^
// `~` on a token: no space between `<` and the name
grammar tag {
    open = _lt, name: ~name, _gt, ~"!"?
    name := "\a"+
    _lt  := "<"
    _gt  := ">"
    space := " "*
}
parse("<a >", tag)^
parse("<a>!", tag)^
parse("<a> !", tag) ^ { ^.message }
parse("< a>", tag) ^ { ^.message }

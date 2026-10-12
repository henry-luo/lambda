// lambda.grammar.jq: the jq 1.7.1 language as a Lambda grammar, ported from
// jq's bison grammar (src/parser.y) and flex lexer (src/lexer.l).
// `parse(text, jq)` turns a jq program into a Lambda element tree; running it
// is not this package's job.
//
// Tree-sitter style (vibe/Lambda_Design_Grammar_Parser.md §5, M4): bison's
// precedence declarations become the levels of one left-recursive `Exp`, with
// `left` / `op` / `right` labels. Levels, loosest first, in bison's order:
// 1 `def …;` before its body (FUNCDEF), 2 `|` and the bodies of `… as $x |`
// and `label $x |`, 3 `,`, 4 `//`, 5 assignments (non-associative), 6 or,
// 7 and, 8 comparisons (non-associative), 9 + - and unary -, 10 * / %,
// 12 postfix `?`, 13 `try`, 14 `try … catch`. `Term` is jq's Term: a
// left-recursive chain of `.name`, `."str"`, `[e]`, `[a:b]` and `[]`
// suffixes, each with an optional `?`. `ObjVal` is jq's ExpD, the value of an
// object entry. Operators are symbol tokens (GM4.2); keywords and punctuation
// are hidden. String interpolation uses `~` (GM13): a piece of string text
// starts exactly where the quote or the interpolation's `)` ended.
//
// Known gaps against jq: string text is raw source (escapes are not
// decoded); in `try A catch B` the body A is read at the loosest level (jq
// requires it to bind tighter than every binary operator, so this grammar also
// accepts `try a | b catch c`); `.a ?// b` reads as `(.a?) // b` where jq's
// lexer reports `?//` outside a pattern.

pub grammar jq {
    Program = Module?, Import*, (body: Exp | FuncDef*)

    Module = _module, metadata: Exp, _semi
    Import = _import, path: String, _as, alias: Variable, metadata: Exp?, _semi
           | _import, path: String, _as, alias: ident, metadata: Exp?, _semi
           | _include, path: String, metadata: Exp?, _semi
    FuncDef = _def, !keyword, name: ident,
              (_lparen, Param, (_semi, Param)*, _rparen)?, _colon, body: Exp, _semi
    Param = name: binding | name: ident

    Exp = prec.right(1, def: FuncDef, body: Exp)
        | prec.right(2, source: Term, _as, _Patterns, _pipe, body: Exp)
        | prec.right(2, _label, label: binding, _pipe, body: Exp)
        | Reduce | Foreach | If
        | prec(14, _try, body: Exp, _catch, catch: Exp)
        | prec.right(13, _try, body: Exp)
        | prec.left(9, op: minus, operand: Exp)
        | prec.right(2, left: Exp, op: pipe, right: Exp)
        | prec.left(3, left: Exp, op: comma, right: Exp)
        | prec.right(4, left: Exp, op: alternative, right: Exp)
        | prec.none(5, left: Exp, op: assign, right: Exp)
        | prec.left(6, left: Exp, op: or, right: Exp)
        | prec.left(7, left: Exp, op: and, right: Exp)
        | prec.none(8, left: Exp, op: compare, right: Exp)
        | prec.left(9, left: Exp, op: additive, right: Exp)
        | prec.left(10, left: Exp, op: multiplicative, right: Exp)
        | prec(12, operand: Exp, op: question)
        | Term

    Reduce = _reduce, source: Term, _as, _Patterns,
             _lparen, init: Exp, _semi, update: Exp, _rparen
    Foreach = _foreach, source: Term, _as, _Patterns,
              _lparen, init: Exp, _semi, update: Exp, (_semi, extract: Exp)?, _rparen
    If = _if, condition: Exp, _then, then: Exp, Elif*, (_else, else: Exp)?, _end
    Elif = _elif, condition: Exp, _then, then: Exp

    // destructuring: `?//` separates alternative patterns
    _Patterns = _Pattern, (_alternation, _Pattern)*
    _Pattern = Variable | ArrayPattern | ObjectPattern
    ArrayPattern = _lbracket, _Pattern, (_comma, _Pattern)*, _rbracket
    ObjectPattern = _lbrace, PatternEntry, (_comma, PatternEntry)*, _rbrace
    PatternEntry = key: Variable, (_colon, value: _Pattern)?
                 | key: ident, _colon, value: _Pattern
                 | key: String, _colon, value: _Pattern
                 | _lparen, key: Exp, _rparen, _colon, value: _Pattern

    Term = prec(1, base: Term, _dot, field: ~name, optional: question?)
         | prec(1, base: Term, _dot, field: String, optional: question?)
         | prec(1, base: Term, _dot?, iterate: Iterate, optional: question?)
         | prec(1, base: Term, _dot?, slice: Slice, optional: question?)
         | prec(1, base: Term, _dot?, _lbracket, index: Exp, _rbracket, optional: question?)
         | Number
         | _dot, field: ~name, optional: question?
         | _dot, field: String, optional: question?
         | Recurse | Identity
         | String | Format
         | Parenthesized | Array | Object
         | Loc | Variable | Break | FunctionCall
    Iterate = _lbracket, _rbracket
    Slice = _lbracket, from: Exp, _colon, to: Exp?, _rbracket
          | _lbracket, _colon, to: Exp, _rbracket

    Number = number
    Recurse = _dotdot
    Identity = _dot
    String = format: format?, _dq, (~text | Interpolation)*, ~_dq
    Interpolation = ~_interp_open, Exp, _rparen
    Format = name: format
    Parenthesized = _lparen, Exp, _rparen
    Array = _lbracket, Exp?, _rbracket
    Object = _lbrace, (Pair, (_comma, Pair)*, _comma?)?, _rbrace
    Loc = _loc, !~"\w"
    Variable = name: binding
    Break = _break, label: binding
    FunctionCall = !keyword, name: ident, (_lparen, Exp, (_semi, Exp)*, _rparen)?

    // an object entry: `{a, $b, "c": 1, (.d): 2, a: .x | .y}`
    Pair = key: Loc
         | key: Variable, (_colon, value: ObjVal)?
         | key: ident, (_colon, value: ObjVal)?
         | key: String, (_colon, value: ObjVal)?
         | _lparen, key: Exp, _rparen, _colon, value: ObjVal
    ObjVal = prec.right(1, left: ObjVal, op: pipe, right: ObjVal)
           | prec.right(2, op: minus, operand: ObjVal)
           | Term

    // tokens: names, numbers and string text emit strings
    ident   := (("\a" | "_") "\w"* "::")* ("\a" | "_") "\w"*
    name    := ("\a" | "_") "\w"*
    binding := "$" ident
    number  := ("\d"+ ("." "\d"*)? | "." "\d"+) (("e" | "E") ("+" | "-")? "\d"+)?
    format  := "@" "\w"+
    text    := ("\\u" ("\a" | "\d"){0,4} | "\\" !("u" | "(") | !("\\" | "\""))+
    keyword := "include" | "foreach" | "import" | "module" | "reduce" | "break" | "catch"
             | "label" | "elif" | "else" | "then" | "and" | "def" | "end" | "try"
             | "as" | "if" | "or"
    space   := ("\s" | "#" !"\n"*)+

    // tokens: operators emit symbols
    pipe        := '|'
    comma       := ','
    alternative := '//'
    assign      := '//=' | '|=' | '+=' | '-=' | '*=' | '/=' | '%=' | '='
    or          := 'or'
    and         := 'and'
    compare     := '==' | '!=' | '<=' | '>=' | '<' | '>'
    additive    := '+' | '-'
    minus       := '-'
    multiplicative := '*' | '/' | '%'
    question    := '?'

    // tokens: structure, emitting nothing
    _dq          := "\""
    _interp_open := "\\("
    _lparen      := "("
    _rparen      := ")"
    _lbracket    := "["
    _rbracket    := "]"
    _lbrace      := "{"
    _rbrace      := "}"
    _semi        := ";"
    _pipe        := "|"
    _colon       := ":"
    _comma       := ","
    _dotdot      := ".."
    _dot         := "."
    _alternation := "?//"
    _loc         := '$__loc__'
    _module      := 'module'
    _import      := 'import'
    _include     := 'include'
    _as          := 'as'
    _def         := 'def'
    _if          := 'if'
    _then        := 'then'
    _elif        := 'elif'
    _else        := 'else'
    _end         := 'end'
    _reduce      := 'reduce'
    _foreach     := 'foreach'
    _try         := 'try'
    _catch       := 'catch'
    _label       := 'label'
    _break       := 'break'
}

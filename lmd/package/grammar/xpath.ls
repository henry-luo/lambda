// lambda.grammar.xpath: XPath 3.1 (W3C Recommendation, 2017-03-21) as a
// Lambda grammar. `parse(text, xpath)` turns an XPath expression into a Lambda
// element tree; evaluating it is not this package's job.
//
// Tree-sitter style (vibe/Lambda_Design_Grammar_Parser.md §5, P5 ruling): one
// directly left-recursive `Expr` carries the operator ladder of the spec's
// OrExpr … PostfixExpr productions as precedence levels, with `left` / `op` /
// `right` labels, so `1 + 2` is <Expr left: <Expr …>, op: '+', right: <Expr …>>.
// Operators and keywords are symbol tokens (GM4.2); structural punctuation is
// hidden. Levels, loosest first: 1 or, 2 and, 3 comparison (non-associative),
// 4 `||`, 5 to (non-associative), 6 + -, 7 * div idiv mod, 8 union |,
// 9 intersect except, 10–13 instance of / treat as / castable as / cast as,
// 14 `=>`, 15 unary + -, 16 `!`, 17 `/` `//`, 18 postfix [ ] ( ) ?.
//
// Known gaps against the spec: comments `(: … :)` do not nest; the parser
// accepts a superset where the spec restricts an operand to ExprSingle (for
// example `1 + for $x in … return $x`); `instance of` and its kin may chain.

pub grammar xpath {
    XPath = _Sequence
    _Sequence = Expr, (_comma, Expr)*

    Expr = prec.left(1, left: Expr, op: or, right: Expr)
         | prec.left(2, left: Expr, op: and, right: Expr)
         | prec.none(3, left: Expr, op: _Comparison, right: Expr)
         | prec.left(4, left: Expr, op: concat, right: Expr)
         | prec.none(5, left: Expr, op: to, right: Expr)
         | prec.left(6, left: Expr, op: additive, right: Expr)
         | prec.left(7, left: Expr, op: _Multiplicative, right: Expr)
         | prec.left(8, left: Expr, op: _Union, right: Expr)
         | prec.left(9, left: Expr, op: intersect_except, right: Expr)
         | prec(10, left: Expr, op: instance, _of, type: SequenceType)
         | prec(11, left: Expr, op: treat, _as, type: SequenceType)
         | prec(12, left: Expr, op: castable, _as, type: SingleType)
         | prec(13, left: Expr, op: cast, _as, type: SingleType)
         | prec.left(14, left: Expr, op: arrow, function: _ArrowFunctionSpecifier, args: ArgumentList)
         | prec.right(15, op: additive, operand: Expr)
         | prec.left(16, left: Expr, op: bang, right: Expr)
         | prec.left(17, left: Expr, op: slash, right: Expr)
         | prec.right(17, op: slash, right: Expr)
         | prec(18, base: Expr, predicate: Predicate)
         | prec(18, base: Expr, args: ArgumentList)
         | prec(18, base: Expr, lookup: Lookup)
         | _ExprSingle
         | _Primary
         | AxisStep
         | root: slash

    _Comparison = value_comp | is | general_comp
    _Multiplicative = star | multiplicative
    _Union = union | pipe

    // ExprSingle forms; lambda.grammar.xquery overrides `_ExprSingle` (§4.5)
    _ExprSingle = ForExpr | LetExpr | QuantifiedExpr | IfExpr
    ForExpr = _for, Binding, (_comma, Binding)*, _return, return: Expr
    LetExpr = _let, LetBinding, (_comma, LetBinding)*, _return, return: Expr
    QuantifiedExpr = quantifier: quantifier, Binding, (_comma, Binding)*, _satisfies, satisfies: Expr
    IfExpr = _if, _lparen, condition: _Sequence, _rparen, _then, then: Expr, _else, else: Expr
    Binding = _dollar, var: eqname, _in, in: Expr
    LetBinding = _dollar, var: eqname, _assign, value: Expr

    // paths
    AxisStep = axis: reverse_axis, _axis, test: _NodeTest, Predicate*
             | abbrev: dotdot, Predicate*
             | axis: forward_axis, _axis, test: _NodeTest, Predicate*
             | abbrev: at, test: _NodeTest, Predicate*
             | test: _NodeTest, Predicate*
    _NodeTest = _KindTest | NameTest
    NameTest = wildcard | eqname
    Predicate = _lbracket, _Sequence, _rbracket
    ArgumentList = _lparen, (_Argument, (_comma, _Argument)*)?, _rparen
    _Argument = ArgumentPlaceholder | Expr
    ArgumentPlaceholder = _question, &(_comma | _rparen)
    Lookup = _question, key: _KeySpecifier
    _KeySpecifier = ncname | IntegerLiteral | ParenthesizedExpr | star
    _ArrowFunctionSpecifier = VarRef | ParenthesizedExpr | eqname

    // primary expressions
    _Primary = DoubleLiteral | DecimalLiteral | IntegerLiteral | StringLiteral
             | VarRef | ParenthesizedExpr | ContextItemExpr
             | FunctionCall | NamedFunctionRef | InlineFunctionExpr
             | MapConstructor | SquareArrayConstructor | CurlyArrayConstructor
             | UnaryLookup
    DoubleLiteral = double
    DecimalLiteral = decimal
    IntegerLiteral = integer
    nospace StringLiteral = _dquote, value: dq_chars?, _dquote | _squote, value: sq_chars?, _squote
    VarRef = _dollar, name: eqname
    ParenthesizedExpr = _lparen, _Sequence?, _rparen
    ContextItemExpr = _dot, !_dot
    FunctionCall = !(_reserved, _lparen), name: eqname, args: ArgumentList
    NamedFunctionRef = name: eqname, _hash, arity: integer
    InlineFunctionExpr = _function, _lparen, (Param, (_comma, Param)*)?, _rparen,
                         (_as, return: SequenceType)?, body: EnclosedExpr
    Param = _dollar, name: eqname, (_as, type: SequenceType)?
    EnclosedExpr = _lbrace, _Sequence?, _rbrace
    MapConstructor = _map, _lbrace, (MapConstructorEntry, (_comma, MapConstructorEntry)*)?, _rbrace
    MapConstructorEntry = key: Expr, _colon, value: Expr
    SquareArrayConstructor = _lbracket, (Expr, (_comma, Expr)*)?, _rbracket
    CurlyArrayConstructor = _array, EnclosedExpr
    UnaryLookup = _question, key: _KeySpecifier

    // types
    SequenceType = kind: empty_sequence, _lparen, _rparen
                 | item: _ItemType, occurrence: occurrence?
    _ItemType = _KindTest | ItemTest | FunctionTest | MapTest | ArrayTest
              | AtomicOrUnionType | ParenthesizedItemType
    ItemTest = _item, _lparen, _rparen
    FunctionTest = _function, _lparen, any: star, _rparen
                 | _function, _lparen, (SequenceType, (_comma, SequenceType)*)?, _rparen,
                   _as, return: SequenceType
    MapTest = _map, _lparen, any: star, _rparen
            | _map, _lparen, key: AtomicOrUnionType, _comma, value: SequenceType, _rparen
    ArrayTest = _array, _lparen, any: star, _rparen
              | _array, _lparen, SequenceType, _rparen
    AtomicOrUnionType = name: eqname
    ParenthesizedItemType = _lparen, _ItemType, _rparen
    SingleType = name: eqname, optional: question?

    // kind tests
    _KindTest = DocumentTest | ElementTest | AttributeTest | SchemaElementTest
              | SchemaAttributeTest | PITest | CommentTest | TextTest
              | NamespaceNodeTest | AnyKindTest
    AnyKindTest = _node, _lparen, _rparen
    TextTest = _text, _lparen, _rparen
    CommentTest = _comment, _lparen, _rparen
    NamespaceNodeTest = _namespace_node, _lparen, _rparen
    PITest = _pi, _lparen, (name: ncname | name: StringLiteral)?, _rparen
    AttributeTest = _attribute, _lparen, (name: _NameOrWildcard, (_comma, type: eqname)?)?, _rparen
    ElementTest = _element, _lparen,
                  (name: _NameOrWildcard, (_comma, type: eqname, nillable: question?)?)?, _rparen
    _NameOrWildcard = star | eqname
    SchemaElementTest = _schema_element, _lparen, name: eqname, _rparen
    SchemaAttributeTest = _schema_attribute, _lparen, name: eqname, _rparen
    DocumentTest = _document_node, _lparen, (ElementTest | SchemaElementTest)?, _rparen

    // tokens: names and literals emit strings
    ncname   := ("\a" | "_") ("\w" | "-" | ".")*
    eqname   := "Q{" !("{" | "}")* "}" ncname | ncname (":" ncname)?
    wildcard := "*:" ncname | ncname ":*" | "Q{" !("{" | "}")* "}*" | "*"
    double   := ("." "\d"+ | "\d"+ ("." "\d"*)?) ("e" | "E") ("+" | "-")? "\d"+
    decimal  := "." "\d"+ | "\d"+ "." "\d"*
    integer  := "\d"+
    dq_chars := (!"\"" | "\"\"")+
    sq_chars := (!"'" | "''")+
    xcomment := "(:" (!":" | ":"+ !(":" | ")"))* ":"+ ")"
    space    := ("\s" | xcomment)+

    // tokens: operators and keywords emit symbols
    or       := 'or'
    and      := 'and'
    value_comp := 'eq' | 'ne' | 'lt' | 'le' | 'gt' | 'ge'
    is       := 'is'
    general_comp := '<<' | '>>' | '<=' | '>=' | '!=' | '=' | '<' | '>'
    concat   := '||'
    to       := 'to'
    additive := '+' | '-'
    star     := '*'
    multiplicative := 'idiv' | 'div' | 'mod'
    union    := 'union'
    pipe     := '|'
    intersect_except := 'intersect' | 'except'
    instance := 'instance'
    treat    := 'treat'
    castable := 'castable'
    cast     := 'cast'
    arrow    := '=>'
    bang     := '!'
    slash    := '//' | '/'
    quantifier := 'some' | 'every'
    reverse_axis := 'ancestor-or-self' | 'preceding-sibling' | 'ancestor' | 'preceding' | 'parent'
    forward_axis := 'descendant-or-self' | 'following-sibling' | 'descendant' | 'following'
                  | 'attribute' | 'namespace' | 'child' | 'self'
    dotdot   := '..'
    at       := '@'
    question := '?'
    occurrence := '?' | '*' | '+'
    empty_sequence := 'empty-sequence'

    // tokens: structure, emitting nothing
    _comma    := ","
    _lparen   := "("
    _rparen   := ")"
    _lbracket := "["
    _rbracket := "]"
    _lbrace   := "{"
    _rbrace   := "}"
    _dollar   := "$"
    _axis     := "::"
    _assign   := ":="
    _colon    := ":"
    _hash     := "#"
    _question := "?"
    _dot      := "."
    _dquote   := "\""
    _squote   := "'"
    _for      := 'for'
    _let      := 'let'
    _return   := 'return'
    _in       := 'in'
    _satisfies := 'satisfies'
    _if       := 'if'
    _then     := 'then'
    _else     := 'else'
    _of       := 'of'
    _as       := 'as'
    _function := 'function'
    _map      := 'map'
    _array    := 'array'
    _item     := 'item'
    _node     := 'node'
    _text     := 'text'
    _comment  := 'comment'
    _attribute := 'attribute'
    _element  := 'element'
    _namespace_node := 'namespace-node'
    _pi       := 'processing-instruction'
    _schema_element := 'schema-element'
    _schema_attribute := 'schema-attribute'
    _document_node := 'document-node'
    _reserved := 'attribute' | 'comment' | 'document-node' | 'element' | 'empty-sequence'
               | 'function' | 'if' | 'item' | 'map' | 'namespace-node' | 'node'
               | 'processing-instruction' | 'schema-attribute' | 'schema-element'
               | 'switch' | 'text' | 'typeswitch' | 'array'
}

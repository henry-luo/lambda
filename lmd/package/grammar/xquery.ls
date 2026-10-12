// lambda.grammar.xquery: XQuery 3.1 (W3C Recommendation, 2017-03-21) as a
// Lambda grammar, composed over lambda.grammar.xpath (§4.5 composition).
// `parse(text, xquery)` turns a main or library module into a Lambda element
// tree; evaluating it is not this package's job.
//
// Tree-sitter style, like the XPath package it imports (vibe/
// Lambda_Design_Grammar_Parser.md M5): XPath's expression ladder, paths,
// literals and types are reused unchanged; this grammar adds the prolog, and
// overrides `_ExprSingle` (full FLWOR, switch, typeswitch, try/catch),
// `_Primary` (constructors, ordered/unordered, validate, extension
// expressions, string constructors), `Binding` and `LetBinding` (type
// declarations) and `InlineFunctionExpr` (annotations). Every XPath rule that
// reaches an overridden rule sees the override.
//
// Direct constructors are written with `~` (GM13), Tree-sitter's
// token.immediate: element content and attribute-value text start exactly
// where the previous token ended, so their whitespace is kept, while the
// expression inside `{ … }` skips space and comments as usual. Content text
// is raw source: entity and character references and doubled braces stay as
// written. A string constructor's text pieces carry their delimiters
// (``"``[a`{"``, ``"}`b]``"``): its text ends at a multi-character delimiter,
// which only a token that includes the delimiter can find.
//
// Known gaps against the spec: comments `(: … :)` do not nest; an end tag is
// not checked against its start tag (no backreferences; GM-O6 leaves that to
// the consumer); whitespace between attributes is optional; `validate` and
// extension expressions take postfix steps like a primary expression.

import lambda.grammar.xpath

pub grammar xquery {
    Module = VersionDecl?, (LibraryModule | MainModule)
    import xpath

    VersionDecl = _xquery, (_encoding, encoding: StringLiteral
                           | _version, version: StringLiteral, (_encoding, encoding: StringLiteral)?),
                  _semicolon
    LibraryModule = ModuleDecl, _Prolog
    MainModule = _Prolog, QueryBody
    ModuleDecl = _module, _namespace, prefix: ncname, _eq, uri: StringLiteral, _semicolon
    QueryBody = _Sequence

    // the prolog: setters, namespaces and imports first, then declarations
    _Prolog = (_PrologSetup, _semicolon)*, (_PrologDecl, _semicolon)*
    _PrologSetup = DefaultNamespaceDecl | BoundarySpaceDecl | DefaultCollationDecl
                 | BaseURIDecl | ConstructionDecl | OrderingModeDecl | EmptyOrderDecl
                 | CopyNamespacesDecl | DecimalFormatDecl | NamespaceDecl
                 | SchemaImport | ModuleImport
    _PrologDecl = ContextItemDecl | VarDecl | FunctionDecl | OptionDecl
    DefaultNamespaceDecl = _declare, _default, kind: element_or_function, _namespace, uri: StringLiteral
    BoundarySpaceDecl = _declare, _boundary_space, mode: preserve_or_strip
    DefaultCollationDecl = _declare, _default, _collation, uri: StringLiteral
    BaseURIDecl = _declare, _base_uri, uri: StringLiteral
    ConstructionDecl = _declare, _construction, mode: preserve_or_strip
    OrderingModeDecl = _declare, _ordering, mode: ordering_mode
    EmptyOrderDecl = _declare, _default, _order, _empty, mode: empty_order
    CopyNamespacesDecl = _declare, _copy_namespaces, preserve: preserve_mode, _comma, inherit: inherit_mode
    DecimalFormatDecl = _declare, (_decimal_format, name: eqname | _default, _decimal_format), DFProperty*
    DFProperty = name: df_property, _eq, value: StringLiteral
    NamespaceDecl = _declare, _namespace, prefix: ncname, _eq, uri: StringLiteral
    SchemaImport = _import, _schema,
                   (_namespace, prefix: ncname, _eq | default: DefaultElementNamespace)?,
                   uri: StringLiteral, (_at, location: StringLiteral, (_comma, location: StringLiteral)*)?
    DefaultElementNamespace = _default, _element, _namespace
    ModuleImport = _import, _module, (_namespace, prefix: ncname, _eq)?,
                   uri: StringLiteral, (_at, location: StringLiteral, (_comma, location: StringLiteral)*)?
    ContextItemDecl = _declare, _context, _item, (_as, type: _ItemType)?,
                      (_assign, value: Expr | External, (_assign, default: Expr)?)
    VarDecl = _declare, Annotation*, _variable, _dollar, name: eqname, (_as, type: SequenceType)?,
              (_assign, value: Expr | External, (_assign, default: Expr)?)
    FunctionDecl = _declare, Annotation*, _function, name: eqname,
                   _lparen, (Param, (_comma, Param)*)?, _rparen, (_as, return: SequenceType)?,
                   (body: EnclosedExpr | External)
    OptionDecl = _declare, _option, name: eqname, value: StringLiteral
    External = _external
    Annotation = _percent, name: eqname, (_lparen, _Literal, (_comma, _Literal)*, _rparen)?
    _Literal = StringLiteral | DoubleLiteral | DecimalLiteral | IntegerLiteral

    // ExprSingle, as XQuery has it
    _ExprSingle = FLWORExpr | QuantifiedExpr | SwitchExpr | TypeswitchExpr | IfExpr | TryCatchExpr
    FLWORExpr = _InitialClause, _IntermediateClause*, _return, return: Expr
    _InitialClause = ForClause | TumblingWindowClause | SlidingWindowClause | LetClause
    _IntermediateClause = _InitialClause | WhereClause | GroupByClause | OrderByClause | CountClause
    ForClause = _for, ForBinding, (_comma, ForBinding)*
    ForBinding = _dollar, var: eqname, (_as, type: SequenceType)?, AllowingEmpty?,
                 (_at, _dollar, position: eqname)?, _in, in: Expr
    AllowingEmpty = _allowing, _empty
    LetClause = _let, LetBinding, (_comma, LetBinding)*
    LetBinding = _dollar, var: eqname, (_as, type: SequenceType)?, _assign, value: Expr
    Binding = _dollar, var: eqname, (_as, type: SequenceType)?, _in, in: Expr
    TumblingWindowClause = _for, _tumbling, _window, _dollar, var: eqname, (_as, type: SequenceType)?,
                           _in, in: Expr, start: WindowStartCondition, end: WindowEndCondition?
    SlidingWindowClause = _for, _sliding, _window, _dollar, var: eqname, (_as, type: SequenceType)?,
                          _in, in: Expr, start: WindowStartCondition, end: WindowEndCondition
    WindowStartCondition = _start, _WindowVars, _when, when: Expr
    WindowEndCondition = only: only?, _end, _WindowVars, _when, when: Expr
    _WindowVars = (_dollar, current: eqname)?, (_at, _dollar, position: eqname)?,
                  (_previous, _dollar, previous: eqname)?, (_next, _dollar, next: eqname)?
    WhereClause = _where, Expr
    GroupByClause = _group, _by, GroupingSpec, (_comma, GroupingSpec)*
    GroupingSpec = _dollar, var: eqname, ((_as, type: SequenceType)?, _assign, value: Expr)?,
                   (_collation, collation: StringLiteral)?
    OrderByClause = stable: stable?, _order, _by, OrderSpec, (_comma, OrderSpec)*
    OrderSpec = Expr, direction: direction?, (_empty, empty: empty_order)?,
                (_collation, collation: StringLiteral)?
    CountClause = _count, _dollar, var: eqname
    SwitchExpr = _switch, _lparen, operand: _Sequence, _rparen, SwitchCaseClause+,
                 _default, _return, default: Expr
    SwitchCaseClause = (_case, case: Expr)+, _return, return: Expr
    TypeswitchExpr = _typeswitch, _lparen, operand: _Sequence, _rparen, CaseClause+,
                     _default, (_dollar, var: eqname)?, _return, default: Expr
    CaseClause = _case, (_dollar, var: eqname, _as)?, SequenceType, (_pipe, SequenceType)*,
                 _return, return: Expr
    TryCatchExpr = _try, body: EnclosedExpr, CatchClause+
    CatchClause = _catch, NameTest, (_pipe, NameTest)*, body: EnclosedExpr

    // primary expressions, as XQuery has them
    _Primary = _DirectConstructor | StringConstructor
             | DoubleLiteral | DecimalLiteral | IntegerLiteral | StringLiteral
             | VarRef | ExtensionExpr | ParenthesizedExpr | ContextItemExpr
             | _ComputedConstructor | OrderedExpr | UnorderedExpr | ValidateExpr
             | FunctionCall | NamedFunctionRef | InlineFunctionExpr
             | MapConstructor | SquareArrayConstructor | CurlyArrayConstructor
             | UnaryLookup
    InlineFunctionExpr = Annotation*, _function, _lparen, (Param, (_comma, Param)*)?, _rparen,
                         (_as, return: SequenceType)?, body: EnclosedExpr
    OrderedExpr = _ordered, body: EnclosedExpr
    UnorderedExpr = _unordered, body: EnclosedExpr
    ValidateExpr = _validate, (mode: validation_mode | _type, type: eqname)?, body: EnclosedExpr
    ExtensionExpr = Pragma+, _lbrace, _Sequence?, _rbrace
    Pragma = _pragma_open, name: eqname, contents: pragma_contents?, _pragma_close
    StringConstructor = sc_whole
                      | sc_start, StringInterpolation, (sc_mid, StringInterpolation)*, sc_end
    StringInterpolation = _Sequence?

    // direct constructors: the text tokens are `~`, so their space is kept
    _DirectConstructor = DirCommentConstructor | DirPIConstructor | CDataSection | DirElemConstructor
    DirElemConstructor = _lt, name: ~qname, DirAttribute*,
                         (_empty_close | _gt, _DirElemContent*, _lt_slash, end: ~qname, _gt)
    DirAttribute = name: qname, _eq, value: DirAttributeValue
    DirAttributeValue = _dquote, (~quot_text | EnclosedExpr)*, ~_dquote
                      | _squote, (~apos_text | EnclosedExpr)*, ~_squote
    _DirElemContent = ~elem_text | _DirectConstructor | EnclosedExpr
    DirCommentConstructor = dir_comment
    DirPIConstructor = dir_pi
    CDataSection = cdata

    // computed constructors
    _ComputedConstructor = CompDocConstructor | CompElemConstructor | CompAttrConstructor
                         | CompNamespaceConstructor | CompTextConstructor
                         | CompCommentConstructor | CompPIConstructor
    CompDocConstructor = _document, body: EnclosedExpr
    CompElemConstructor = _element, (name: eqname | name: EnclosedExpr), body: EnclosedExpr
    CompAttrConstructor = _attribute, (name: eqname | name: EnclosedExpr), body: EnclosedExpr
    CompNamespaceConstructor = _namespace, (prefix: ncname | prefix: EnclosedExpr), uri: EnclosedExpr
    CompTextConstructor = _text, body: EnclosedExpr
    CompCommentConstructor = _comment, body: EnclosedExpr
    CompPIConstructor = _pi, (target: ncname | target: EnclosedExpr), body: EnclosedExpr

    // tokens: direct-constructor text, kept as written
    qname      := ncname (":" ncname)?
    _ref       := "&" ("lt" | "gt" | "amp" | "quot" | "apos") ";"
                | "&#x" ("\d" | "a" to "f" | "A" to "F")+ ";" | "&#" "\d"+ ";"
    elem_text  := (!("{" | "}" | "<" | "&") | "{{" | "}}" | _ref)+
    quot_text  := (!("\"" | "{" | "}" | "<" | "&") | "\"\"" | "{{" | "}}" | _ref)+
    apos_text  := (!("'" | "{" | "}" | "<" | "&") | "''" | "{{" | "}}" | _ref)+
    dir_comment := "<!--" (!"-" | "-" !"-")* "-->"
    dir_pi     := "<?" ncname ("\s" (!"?" | "?"+ !("?" | ">"))* "?"*)? "?>"
    cdata      := "<![CDATA[" (!"]" | "]" !"]" | "]]" "]"* !("]" | ">"))* "]"* "]]>"
    pragma_contents := (!"#" | "#"+ !("#" | ")"))+

    // tokens: a string constructor's text cannot contain "`{" or "]``"; the
    // pieces are an automaton for that language, so a piece ends just before
    // its delimiter
    _sc_loop   := ("]" | "`" "]")*
    _sc_exit   := !("`" | "]") | "`" !("{" | "`" | "]")
    _sc_unit   := !("`" | "]") | "`"+ !("{" | "`" | "]") | ("]" | "`"+ "]") _sc_loop _sc_exit
    _sc_chars  := _sc_unit* ("`"* | ("]" | "`"+ "]") _sc_loop "`"?)
    sc_whole   := "``[" _sc_chars "]``"
    sc_start   := "``[" _sc_chars "`{"
    sc_mid     := "}`" _sc_chars "`{"
    sc_end     := "}`" _sc_chars "]``"

    // tokens emitting symbols
    element_or_function := 'element' | 'function'
    preserve_or_strip   := 'preserve' | 'strip'
    ordering_mode       := 'unordered' | 'ordered'
    empty_order         := 'greatest' | 'least'
    preserve_mode       := 'no-preserve' | 'preserve'
    inherit_mode        := 'no-inherit' | 'inherit'
    df_property         := 'decimal-separator' | 'grouping-separator' | 'infinity'
                         | 'minus-sign' | 'NaN' | 'percent' | 'per-mille' | 'zero-digit'
                         | 'digit' | 'pattern-separator' | 'exponent-separator'
    direction           := 'ascending' | 'descending'
    validation_mode     := 'lax' | 'strict'
    stable              := 'stable'
    only                := 'only'

    // tokens: structure, emitting nothing
    _semicolon   := ";"
    _eq          := "="
    _percent     := "%"
    _pipe        := "|"
    _lt_slash    := "</"
    _empty_close := "/>"
    _lt          := "<"
    _gt          := ">"
    _pragma_open := "(#"
    _pragma_close := "#)"
    _xquery      := 'xquery'
    _version     := 'version'
    _encoding    := 'encoding'
    _module      := 'module'
    _namespace   := 'namespace'
    _declare     := 'declare'
    _default     := 'default'
    _boundary_space := 'boundary-space'
    _collation   := 'collation'
    _base_uri    := 'base-uri'
    _construction := 'construction'
    _ordering    := 'ordering'
    _order       := 'order'
    _empty       := 'empty'
    _copy_namespaces := 'copy-namespaces'
    _decimal_format := 'decimal-format'
    _import      := 'import'
    _schema      := 'schema'
    _at          := 'at'
    _context     := 'context'
    _variable    := 'variable'
    _external    := 'external'
    _option      := 'option'
    _allowing    := 'allowing'
    _tumbling    := 'tumbling'
    _sliding     := 'sliding'
    _window      := 'window'
    _start       := 'start'
    _end         := 'end'
    _when        := 'when'
    _previous    := 'previous'
    _next        := 'next'
    _where       := 'where'
    _group       := 'group'
    _by          := 'by'
    _count       := 'count'
    _switch      := 'switch'
    _case        := 'case'
    _typeswitch  := 'typeswitch'
    _try         := 'try'
    _catch       := 'catch'
    _document    := 'document'
    _ordered     := 'ordered'
    _unordered   := 'unordered'
    _validate    := 'validate'
    _type        := 'type'
}

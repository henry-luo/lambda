// lambda.grammar.rnc: RELAX NG Compact Syntax (OASIS Committee Specification,
// 2002-11-21) as a Lambda grammar. `parse(text, rnc)` turns a compact schema
// into a Lambda element tree; converting it to RELAX NG XML or validating with
// it is not this package's job.
//
// Tree-sitter style (vibe/Lambda_Design_Grammar_Parser.md M5). The spec does
// not let `,` `&` `|` mix without parentheses, so each is its own repetition
// (`Group`, `Interleave`, `Choice`) over particles rather than a precedence
// ladder; parentheses leave no node. A literal's segments join with `~` and
// lose their quotes: the text pieces are `~` tokens (GM13), so the quote that
// opens a segment is not followed by skipped space, and adjacent pieces merge
// into one string (S2.6.4) exactly as `~` concatenates them. Keywords are
// reserved for identifiers (write `\element` to name a definition `element`)
// and free for element and attribute names.
//
// Known gaps against the spec: `\x{…}` escapes are not expanded; a keyword is
// recognized by the identifier bytes of Lambda, so the structural keywords
// (`element`, `start`, …) also match the front of a name like `element-x`,
// which then parses by backtracking; documentation comments are accepted only
// before grammar items, particles and annotations.

pub grammar rnc {
    Schema = _Decl*, (_GrammarItem+ | _Pattern)?

    _Decl = NamespaceDecl | DefaultNamespaceDecl | DatatypesDecl
    NamespaceDecl = _namespace, prefix: _IdentOrKeyword, _eq, uri: _NamespaceUri
    DefaultNamespaceDecl = _default, _namespace, prefix: _IdentOrKeyword?, _eq, uri: _NamespaceUri
    DatatypesDecl = _datatypes, prefix: _IdentOrKeyword, _eq, uri: Literal
    _NamespaceUri = Literal | Inherit

    // grammar content
    _GrammarItem = Start | Define | Div | Include | AnnotationElement
    Start = Documentation?, Annotations?, _start, assign: assign, _Pattern
    Define = Documentation?, Annotations?, name: _Ident, assign: assign, _Pattern
    Div = Documentation?, Annotations?, _div, _lbrace, _GrammarItem*, _rbrace
    Include = Documentation?, Annotations?, _include, uri: Literal, inherit: Inherit?,
              (_lbrace, _GrammarItem*, _rbrace)?
    Inherit = _inherit, _eq, prefix: _IdentOrKeyword

    // patterns: the three binary forms do not mix without parentheses
    _Pattern = Group | Interleave | Choice | _Particle
    Group = _Particle, (_comma, _Particle)+
    Interleave = _Particle, (_amp, _Particle)+
    Choice = _Particle, (_pipe, _Particle)+
    memo _Particle = Repeat | _Annotated
    Repeat = pattern: _Annotated, op: occurrence, FollowAnnotation*
    _Annotated = Annotated | _Primary
    Annotated = Documentation, Annotations?, pattern: _Primary, FollowAnnotation*
              | Annotations, pattern: _Primary, FollowAnnotation*
              | pattern: _Primary, FollowAnnotation+

    _Primary = Element | Attribute | List | Mixed | _lparen, _Pattern, _rparen
             | ParentRef | Empty | Text | NotAllowed | External | GrammarBlock
             | Value | Data | Ref
    Element = _element, name: _NameClass, _lbrace, _Pattern, _rbrace
    Attribute = _attribute, name: _NameClass, _lbrace, _Pattern, _rbrace
    List = _list, _lbrace, _Pattern, _rbrace
    Mixed = _mixed, _lbrace, _Pattern, _rbrace
    ParentRef = _parent, name: _Ident
    Empty = _empty
    Text = _text
    NotAllowed = _not_allowed
    External = _external, uri: Literal, inherit: Inherit?
    GrammarBlock = _grammar, _lbrace, _GrammarItem*, _rbrace
    Value = type: _DatatypeName?, value: Literal
    Data = type: _DatatypeName, (_lbrace, Param*, _rbrace)?, (_minus, except: _Annotated)?
    Param = Documentation?, Annotations?, name: _IdentOrKeyword, _eq, value: Literal
    Ref = name: _Ident
    _DatatypeName = cname | builtin

    // name classes
    _NameClass = NameChoice | _SimpleNameClass
    NameChoice = _SimpleNameClass, (_pipe, _SimpleNameClass)+
    memo _SimpleNameClass = NsName | AnyName | Name | _lparen, _NameClass, _rparen
    NsName = prefix: ncname, ~_colon_star, (_minus, except: _SimpleNameClass)?
    AnyName = _star, (_minus, except: _SimpleNameClass)?
    Name = cname | _IdentOrKeyword

    // annotations
    Annotations = _lbracket, AnnotationAttribute*, (AnnotationElement | Literal)*, _rbracket
    AnnotationAttribute = name: _AnnotationName, _eq, value: Literal
    AnnotationElement = name: _AnnotationName, _lbracket,
                        AnnotationAttribute*, (AnnotationElement | Literal)*, _rbracket
    FollowAnnotation = _follow, AnnotationElement
    Documentation = doc
    _AnnotationName = cname | _IdentOrKeyword

    // names: `\name` escapes a keyword, so the backslash is not part of it
    _Ident = _backslash, ~ncname | !(keyword, !~name_char), ncname
    _IdentOrKeyword = _backslash, ~ncname | ncname

    // a literal: quoted segments joined by `~`, the quotes dropped
    Literal = _Segment, (_tilde, _Segment)*
    _Segment = _tdq, ~tdq_text?, ~_tdq
             | _tsq, ~tsq_text?, ~_tsq
             | _dq, ~dq_text?, ~_dq
             | _sq, ~sq_text?, ~_sq

    // tokens emitting strings
    name_char := "\w" | "-" | "."
    ncname   := ("\a" | "_") name_char*
    cname    := ncname ":" ncname
    builtin  := "string" | "token"
    dq_text  := !("\"" | "\n")+
    sq_text  := !("'" | "\n")+
    tdq_text := ("\""? "\""? !"\"")+
    tsq_text := ("'"? "'"? !"'")+
    doc      := "##" !"\n"* ("\n" (" " | "\t")* "##" !"\n"*)*
    keyword  := "attribute" | "datatypes" | "default" | "div" | "element" | "empty"
              | "external" | "grammar" | "include" | "inherit" | "list" | "mixed"
              | "namespace" | "notAllowed" | "parent" | "start" | "string" | "text"
              | "token"
    space    := ("\s" | "#" !("#" | "\n") !"\n"* | "#\n")+

    // tokens emitting symbols
    assign     := '|=' | '&=' | '='
    occurrence := '?' | '*' | '+'

    // tokens: structure, emitting nothing
    _lbrace    := "{"
    _rbrace    := "}"
    _lbracket  := "["
    _rbracket  := "]"
    _lparen    := "("
    _rparen    := ")"
    _comma     := ","
    _amp       := "&"
    _pipe      := "|"
    _eq        := "="
    _minus     := "-"
    _star      := "*"
    _tilde     := "~"
    _follow    := ">>"
    _backslash := "\\"
    _colon_star := ":*"
    _tdq       := "\"\"\""
    _tsq       := "'''"
    _dq        := "\""
    _sq        := "'"
    _attribute := 'attribute'
    _datatypes := 'datatypes'
    _default   := 'default'
    _div       := 'div'
    _element   := 'element'
    _empty     := 'empty'
    _external  := 'external'
    _grammar   := 'grammar'
    _include   := 'include'
    _inherit   := 'inherit'
    _list      := 'list'
    _mixed     := 'mixed'
    _namespace := 'namespace'
    _not_allowed := 'notAllowed'
    _parent    := 'parent'
    _start     := 'start'
    _text      := 'text'
}

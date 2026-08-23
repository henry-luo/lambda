/**
 * @file Lambda Script grammar for tree-sitter — THE OFFICIAL GRAMMAR
 * @author Henry Luo
 * @license MIT
 *
 * This single file is the normative surface grammar. It implements
 * `S16 Surface Syntax` (doc/Lambda_Formal_Semantics.md) and the §7 audit
 * rulings recorded in vibe/Lambda_Design_Syntax.md.
 *
 * ROLE (Design_Syntax §4.4). Tree-sitter is the OFFICIAL GRAMMAR and the
 * cross-checking reference implementation; the hand-written C
 * recursive-descent parser in lambda/runtime/parser/ is PRODUCTION. Because
 * this grammar no longer ships in the parse path, table size and speed stop
 * being constraints — which is why the former three-file split
 * (grammar.js + grammar-lambda.js + grammar-common.js) and the seven
 * sub-language extraction tokens are gone. Type patterns, view patterns, and
 * path bodies are ordinary rules again, so this file states the whole
 * language in one readable artifact.
 *
 * The external scanner survives with one job only: newline awareness, which
 * grammar rules cannot express because `/\s/` lives in `extras`. It emits
 * three zero-width guards — `_join`, `_stmt_boundary`, `_not_paren`. See
 * src/scanner.c.
 *
 * AUTHORITY ORDER: spec/design doc -> this grammar -> C parser. A divergence
 * anywhere downstream is a bug in the downstream artifact.
 */

// @ts-check
/// <reference types="../tree-sitter-dsl.d.ts" />

function comma_sep1(rule) {
  return seq(rule, repeat(seq(',', rule)));
}

function comma_sep(rule) {
  return optional(comma_sep1(rule));
}

// S2.4.3v2: a namespace-qualified name is maximal, so `<svg .rect>` keeps the
// tag `svg.rect` while `<svg, .rect>` (§7.11) is tag + path child.
function qualified_name($, precedence) {
  return prec.left(precedence, seq(
    choice($.identifier, $.symbol),
    repeat1(prec.left(precedence, seq(
      // Either dot may arrive here. In a position where a member expression is
      // also possible — an element interior, where `xml` could equally begin
      // content — the scanner emits the guarded `_member_dot` and the internal
      // high-precedence dot is never produced, so a namespaced NAME must accept
      // both spellings or `<div xml.lang: "en">` cannot parse.
      choice(token(prec(precedence, '.')), alias($._member_dot, '.')),
      choice($.identifier, $.symbol),
    ))),
  ));
}

// --- numeric literals -------------------------------------------------------
// §7.4: `_` is permitted between digits in every numeric family, hex included.
// It is spelling only and never reaches the value. Placement is constrained to
// digit-underscore-digit, so no leading, trailing, doubled, or suffix-adjacent
// underscore parses.
const dec_digits = /[0-9](_?[0-9])*/;
const hex_digits = /[0-9a-fA-F](_?[0-9a-fA-F])*/;
const integer_literal = choice('0', seq(/[1-9]/, optional(/(_?[0-9])+/)));
const hex_integer_literal = seq('0', choice('x', 'X'), hex_digits);
const exponent_part = seq(choice('e', 'E'), optional(choice('+', '-')), dec_digits);
// C16 ruling 9: an unsuffixed literal's type is LEXICAL, and an EXPONENT makes
// it a float — `1e2` is float 100.0, as in C, Python, Java, Go, Rust and Swift.
const float_literal = choice(
  seq(integer_literal, '.', dec_digits, optional(exponent_part)),
  seq('.', dec_digits, optional(exponent_part)),
  seq(integer_literal, exponent_part),
);
const decimal_literal = choice(
  seq(integer_literal, '.', dec_digits),
  seq('.', dec_digits),
);

const sized_int_suffix = choice('i8', 'i16', 'i32', 'i64', 'u8', 'u16', 'u32', 'u64');
const sized_float_suffix = choice('f16', 'f32', 'f64');

// --- binary operator table --------------------------------------------------
// S16.2.3: an operator that can also START an expression is dual-role, so a
// line may not begin with it. The `_join` guard is what enforces that — the
// scanner emits it only when the operator is on the same line as its left
// operand. Operators that can only ever continue (`|> | & % > == != <= >=`,
// `++`, `**`, and every word operator) carry no guard, so they are free to
// open a line (S16.2.2).
//
// `in_element` drops the symbol relationals entirely: inside an element, `<`
// and `>` are not operators at all, they delimit (S16.5.1 / §5.10).
function binary_rules($, in_element) {
  const operand = in_element
    ? choice($.primary_expr, $.unary_expr, $.not_expr,
        alias($.element_binary_expr, $.binary_expr))
    : $._expr;
  const mk = (operator, precedence, assoc) => wrap(assoc)(precedence, seq(
    field('left', operand), field('operator', operator), field('right', operand),
  ));
  const wrap = assoc => (assoc === 'right' ? prec.right : prec.left);
  const rules = [
    mk(alias($._bin_plus, '+'), 'binary_plus', 'left'),
    mk(alias($._bin_minus, '-'), 'binary_plus', 'left'),
    mk('++', 'binary_plus', 'left'),
    mk(alias($._bin_star, '*'), 'binary_times', 'left'),
    mk(alias($._bin_slash, '/'), 'binary_times', 'left'),
    mk('div', 'binary_times', 'left'),
    mk('%', 'binary_times', 'left'),
    mk('**', 'binary_pow', 'right'),
    mk($._binary_eq_symbol_op, 'binary_eq', 'left'),
    mk($._binary_eq_word_op, 'binary_eq', 'left'),
    mk($._binary_word_relation_op, 'binary_relation', 'left'),
    mk('and', 'logical_and', 'left'),
    mk('or', 'logical_or', 'left'),
    mk('to', 'range_to', 'left'),
    mk('|', 'set_union', 'left'),
    mk('|>', 'pipe', 'left'),
    mk('that', 'pipe', 'left'),
    mk('&', 'set_intersect', 'left'),
    // §7.1 removed unary `!` from value expressions, so infix `!` (set
    // exclusion) is unguarded: it can only continue.
    mk('!', 'set_exclude', 'left'),
    mk('is', 'is_in', 'left'),
    mk('in', 'is_in', 'left'),
    mk($._at, 'is_in', 'left'),
  ];
  if (!in_element) {
    rules.push(
      mk(alias($._bin_lt, '<'), 'binary_relation', 'left'),
      mk('<=', 'binary_relation', 'left'),
      mk('>=', 'binary_relation', 'left'),
      mk('>', 'binary_relation', 'left'),
    );
  }
  return rules;
}

function type_operators(type_expr) {
  return [
    ['|', 'set_union'],
    ['&', 'set_intersect'],
    ['!', 'set_exclude'],
  ].map(([operator, precedence]) => prec.left(precedence, seq(
    field('left', type_expr),
    field('operator', operator),
    field('right', type_expr),
  )));
}

module.exports = grammar({
  name: "lambda",

  extras: $ => [/\s/, $.comment],

  word: $ => $.identifier,

  externals: $ => [
    // Guarded operators (S16.2.3). Each consumes its own lexeme and the scanner
    // emits it only when the operator shares a line with its left operand, so a
    // line-start `+ - * / < ( [ . ^` can neither continue the previous
    // expression nor (see `_stmt_boundary`) open a new statement: it is an
    // error, which is the whole point. They are real tokens rather than one
    // zero-width marker so operator precedence still resolves at one-token
    // lookahead.
    $._bin_plus,
    $._bin_minus,
    $._bin_star,
    $._bin_slash,
    $._bin_lt,
    $._call_lparen,
    $._index_lbracket,
    // Same line, or across a break for the S16.2.4 `.ident(` member-call form.
    $._member_dot,
    $._postfix_caret,
    // A new statement starts here: emitted only before a start-only token,
    // which is disjoint from every guarded operator above.
    $._stmt_boundary,
    // S16.5.1: the element-scope variant, where `<` starts a child item.
    $._elem_stmt_boundary,
    // The next token is not `(`: gates bare `if`/`while` heads (S16.6.2) and
    // the bare `apply` statement (§7.7).
    $._not_paren,
    // §7.16: a numeric literal may not run straight into an identifier.
    $._num_boundary,
    // Never valid in the grammar; its presence means error recovery.
    $._error_sentinel,
  ],

  conflicts: $ => [
    // After an attribute, a `,` either separates another attribute or is the
    // required attr-list -> content boundary (§7.11v2). Which one takes two
    // tokens to see, so GLR forks and the losing branch dies immediately.
    [$._attr_list],
    // §7.22 made `a?` ambiguous inside a type body: an optional FIELD marker
    // (`a?: T`) or an occurrence TYPE used as content (`a?`). The `:` decides,
    // one token later.
    // §7.22 made a leading name inside a type body ambiguous: a FIELD name
    // (`a?: T`, `a: T`) or a bare content TYPE (`a?`, `a`). The `:` decides,
    // one or two tokens later.
    [$._field_name, $.primary_type],
    [$._field_name, $.base_type],
  ],

  supertypes: $ => [],

  inline: $ => [
    $._non_null_literal,
    $._parenthesized_expr,
    $._number,
    $._key,
  ],


  precedences: $ => [
    [
      $.fn_expr_stam,
      'propagate',
      $.call_expr,
      $.index_expr,
      'query_expr',
      'member',
      $.primary_expr,
      $.unary_expr,
      'binary_pow',
      'binary_times',
      'binary_plus',
      'binary_relation',
      'binary_eq',
      'range_to',
      'set_intersect',
      'set_exclude',
      'set_union',
      'is_in',
      // §7.2: `not` binds BELOW comparisons and `is`/`in`/`at`, above
      // `and`/`or` — the Python placement, so `not a == b` is `not (a == b)`.
      'logical_not',
      'logical_and',
      'logical_or',
      'pipe',
      $.if_expr,
      $.while_expr,
      $.match_expr,
      $.for_expr,
      $.let_expr,
      $.assign_expr,
      $.assign_stam,
    ],
    [$.element_binary_expr, $._element_expr],
    ['query_expr', $._expr],
    [
      $.range_type,
      $.primary_type,
      $.unary_type,
      $.binary_type,
      $.negation_type,
      $._type_pattern,
      $.return_type,
      $.fn_type,
    ],
  ],

  rules: {
    // ======================= Document and statements =======================

    document: $ => optional($.content),

    // §7.17: `comment` is declared BOTH here and in `externals`. The scanner
    // emits it wherever it runs, so a line break in front of a comment is
    // carried to the next token; this rule is the fallback tree-sitter uses in
    // positions where the scanner is not consulted and during error recovery.
    comment: _ => token(prec(1, choice(
      seq('//', /[^\r\n\u2028\u2029]*/),
      seq('/*', /[^*]*\*+([^/*][^*]*\*+)*/, '/'),
    ))),

    // S16.1.2: `;` is a STRICT separator — between two statements only. There
    // is no trailing form and no empty slot; both are syntax errors.
    // S16.1.3: adjacent statements need no separator at all when the second
    // begins with a start-only token, which `_stmt_boundary` certifies.
    content: $ => $._stam_seq,

    // §7.14: the boundary rule keys off the previous statement's TAIL, not its
    // kind. A CLOSED tail ends in the structural closer of a non-postfixable
    // construct, so no dual-role token can continue it and the next statement
    // simply juxtaposes — "after a block, never `;`". An OPEN tail ends in a
    // greedy expression, so `;` or the `_stmt_boundary` guard is required and a
    // line-start dual-role token is the S16.2.3 error.
    //
    // The classification is DERIVED from the S16.1.1 golden test: a tail is
    // open exactly when the one-line spelling would glue. `fn f() {} [0]` is
    // two items on one line (a declaration is not an expression, and
    // `index_expr` needs a primary), so juxtaposition changes nothing;
    // `let x = a [0]` is ONE item on one line, so accepting a split across
    // lines would change meaning.
    _stam_seq: $ => choice(
      $._open_stam,
      $._closed_stam,
      seq($._open_stam, choice(';', $._stmt_boundary), $._stam_seq),
      seq($._closed_stam, optional(choice(';', $._stmt_boundary)), $._stam_seq),
    ),

    _closed_stam: $ => choice(
      $.fn_stam,
      $.object_type,
      $.view_stam,
      // Both `while` spellings take a structural body, and `match` closes on
      // its arm list, so neither can be continued.
      $.while_expr,
      $.match_expr,
      $.break_stam,
      $.continue_stam,
      $.apply_stam,
      // An import's tail is a module NAME, and no dual-role token can continue
      // one (`,` `:` `.` `\\` are its only continuations), so the `;` the open
      // set would demand guards nothing: `import math [1,2]` is two items on
      // one line as well as two.
      $._import_stam,
      // Bare-spelling `if`/`for` end in a braced body that admits no postfix.
      // Their parenthesized spellings take a greedy expression body and are
      // therefore open, as is any `if` carrying an `else` (the else body is an
      // expression: `if c {} else {} [0]` glues on one line).
      $._if_closed,
      $._for_closed,
    ),

    _open_stam: $ => choice(
      $.let_stam,
      $.var_stam,
      $.fn_expr_stam,
      $.type_stam,
      $.assign_stam,
      $.return_stam,
      $._if_open,
      $._for_open,
      $._expr_tail,
    ),

    // `_expr` minus the four control forms, which the statement level
    // classifies for itself above.
    _expr_tail: $ => choice(
      $.primary_expr,
      $.unary_expr,
      $.not_expr,
      $.binary_expr,
      $.raise_expr,
    ),

    _declaration: $ => choice(
      $._import_stam,
      $.let_stam,
      $.var_stam,
      $.fn_stam,
      $.fn_expr_stam,
      $.type_stam,
      $.object_type,
      $.view_stam,
    ),

    // ============================== Literals ==============================

    string: _ => token(seq(
      '"',
      repeat(choice(
        /[^"\\]+/,
        /\\["\\\/bfnrt]/,
        /\\u[0-9a-fA-F]{4}/,
        /\\u\{[0-9a-fA-F]+\}/,
      )),
      '"',
    )),

    symbol: _ => token(seq(
      "'",
      repeat1(choice(
        /[^'\\\n]+/,
        /\\['\\\/bfnrt]/,
        /\\u[0-9a-fA-F]{4}/,
        /\\u\{[0-9a-fA-F]+\}/,
      )),
      "'",
    )),

    binary: _ => token(seq("b'", repeat1(/[^']/), "'")),

    // §7.16: every numeric literal carries a zero-width boundary guard, so a
    // digit running into an identifier (`123abc`, `0b1010`, `1_`) is a lexical
    // error rather than a number plus a silently juxtaposed statement. This is
    // the general form of the §7.3 `1f32` bug.
    _number: $ => seq(
      choice($.imaginary, $.integer, $.float, $.decimal,
        $.sized_integer, $.sized_float),
      $._num_boundary,
    ),

    imaginary: _ => token(seq(
      choice(float_literal, integer_literal, 'inf', 'nan'), 'j',
    )),
    // §7.5: hex is the only radix prefix; `0b`/`0o` were considered and
    // rejected.
    integer: _ => token(choice(hex_integer_literal, integer_literal)),
    float: _ => token(float_literal),

    decimal: _ => token(seq(
      choice(float_literal, decimal_literal, integer_literal),
      choice('n', 'm'),
    )),

    sized_integer: _ => token(seq(
      choice(hex_integer_literal, integer_literal), sized_int_suffix,
    )),

    // §7.3: integer spellings are accepted, symmetric with `1i32`. Requiring a
    // decimal point made `1f32` lex as two tokens with context-dependent
    // results (bare `1f32` was `1`; `type(1f32)` was the base type `f32`).
    sized_float: _ => token(seq(
      choice(float_literal, decimal_literal, integer_literal), sized_float_suffix,
    )),

    datetime: _ => token(seq("t'", repeat(choice(/[0-9]/, /[:\-+.tTzZ ]/)), "'")),

    named_value: _ => token(choice(
      'decimal.inf', 'decimal.nan', 'true', 'false', 'inf', 'nan',
    )),

    _non_null_literal: $ => choice(
      $._number, $.string, $.symbol, $.datetime, $.binary, $.named_value,
    ),

    // ============================ Containers ==============================

    // Names, not types: a field/attribute may be spelled with a base-type
    // keyword (`type: string`, `string: int`), which is what the C parser's
    // `token_is_key` has always allowed.
    _field_name: $ => choice($.symbol, $.identifier,
      alias($._base_type_kw, $.base_type), alias('type', $.base_type)),

    _key: $ => choice($.symbol, $.identifier,
      alias($._base_type_kw, $.base_type), alias('type', $.base_type),
      $.last_index, '*'),

    map_item: $ => seq(field('name', $._key), ':', field('as', $._expr)),

    // §5.9v3 / S16.4.1. Three disjoint brace forms. `map` and `block` are told
    // apart by their interiors; `empty_braces` is the neutral node for `{}`,
    // whose reading is settled by build_ast from context (value/content
    // position -> empty map; fn control body -> empty map; pn control body ->
    // empty block; bare pn statement -> error).
    map: $ => seq('{', comma_sep1($.map_item), '}'),

    // Block expressions (Rust-style): the value is the last expression and the
    // `let`s are block-scoped. This is what gives arrow functions block bodies
    // with no `({...})` parenthesization quirk.
    block: $ => seq('{', $.content, '}'),

    empty_braces: _ => seq('{', '}'),

    _braced: $ => choice($.block, $.map, $.empty_braces),
    // Declaration bodies are structural: always a block, never a map (§5.9v3).
    _body_block: $ => choice($.block, $.empty_braces),

    array: $ => seq('[', comma_sep($._expr), ']'),

    // ============================= Elements ===============================

    attr_name: $ => choice(alias($._attr_dotted_name, $.dotted_name), $._key),
    _attr_dotted_name: $ => qualified_name($, 120),
    dotted_name: $ => qualified_name($, 120),

    attr: $ => seq(field('name', $.attr_name), ':', field('as', $._element_expr)),

    // §7.11: `;` has left the element. Attributes are a strict comma list
    // (pair-list regime); the attr-list -> content boundary takes an OPTIONAL
    // comma — always permitted, and required exactly where the first content
    // item could otherwise continue the last attribute value (`<div a: x, (y)>`)
    // or where it disambiguates the tag (`<svg, .rect>` vs the maximal-munch
    // qualified tag `<svg .rect>`). Content juxtaposes after that: `<div "s">`.
    element: $ => seq('<',
      field('tag', choice($.dotted_name, $.symbol, $.identifier)),
      optional(choice(
        // Attributes, then content only behind a REQUIRED boundary comma. The
        // comma is also what settles a greedy attribute value: `<div a: x (y)>`
        // makes the call `x(y)` the attribute, `<div a: x, (y)>` makes `(y)`
        // content.
        seq($._attr_list, optional(seq(',', $.element_content))),
        // Content alone takes NO comma: `<div "text">` reads as markup should.
        $.element_content,
      )),
      '>',
    ),

    _attr_list: $ => seq($.attr, repeat(seq(',', $.attr))),

    // S16.5.1: element interiors use the relational-free expression tier, so
    // `>` is unconditionally the terminator and `<` unconditionally opens a
    // child. Parentheses remain islands: `(a > b)` re-enters the full grammar.
    element_content: $ => seq(
      $._element_statement,
      repeat(seq(choice(';', $._elem_stmt_boundary), $._element_statement)),
    ),

    _element_statement: $ => choice(
      $._declaration,
      $._element_expr,
      $.if_expr,
      $.for_expr,
      $.while_expr,
      $.match_expr,
      $.assign_stam,
      $.return_stam,
      $.apply_stam,
    ),

    element_binary_expr: $ => choice(...binary_rules($, true)),

    _element_expr: $ => choice(
      $.primary_expr,
      $.unary_expr,
      $.not_expr,
      alias($.element_binary_expr, $.binary_expr),
    ),

    // ============================ Expressions =============================

    _parenthesized_expr: $ => seq(
      '(',
      choice(
        $._expr,
        seq(repeat1(prec(1, seq($.let_expr, ','))), $._expr),
      ),
      ')',
    ),

    _expr: $ => choice(
      $.primary_expr,
      $.unary_expr,
      $.not_expr,
      $.binary_expr,
      $.if_expr,
      $.while_expr,
      $.match_expr,
      $.for_expr,
      $.raise_expr,
    ),

    raise_expr: $ => prec.right(seq('raise', field('value', $._expr))),

    primary_expr: $ => prec(50, choice(
      $.named_value,
      $.last_index,
      $._number,
      $.datetime,
      $.string,
      $.symbol,
      $.binary,
      $.array,
      $.map,
      $.block,
      $.empty_braces,
      $.element,
      // `type` is deliberately absent from value position: it is the
      // introducer of a type declaration, and admitting it as a bare value
      // would let `type E { … }` read as three juxtaposed statements (S16.1.3)
      // instead of a declaration. `type(x)` is reinstated as a call form below.
      alias($._base_type_kw, $.base_type),
      $.pattern_island,
      $.identifier,
      $.index_expr,
      $.path_expr,
      $.member_expr,
      $.handler_expr,
      $.propagate_expr,
      $.call_expr,
      $.query_expr,
      $._parenthesized_expr,
      $.fn_expr,
      $.current_expr,
      $.current_parent_expr,
      $.current_error_expr,
      $.variadic,
    )),

    // Every postfix form below opens with a dual-role token, so each takes the
    // `_join` guard: on its own line, `(`, `[`, `.`, and `^` are S16.2.3
    // errors rather than silent continuations or silent new statements.
    call_expr: $ => prec.right(100, seq(
      field('function', choice($.primary_expr, 'import',
        alias($._apply_kw, $.identifier),
        // `type(x)` — the keyword is callable even though it is not a bare
        // value. One token of lookahead separates it from a declaration:
        // `(` means call, an identifier means `type Name …`.
        alias('type', $.base_type))),
      alias($._call_lparen, '('),
      comma_sep(field('argument', choice($.named_argument, $._expr))),
      ')',
    )),

    propagate_expr: $ => prec.left(100, seq(
      field('operand', $.primary_expr),
      field('propagate', alias($._postfix_caret, '^')),
    )),

    handler_expr: $ => prec.left(100, seq(
      field('operand', $.primary_expr),
      // The handler brace must open on the same line as its `^` (§3.6); a
      // line-start `{` is always a new map or block expression.
      alias($._postfix_caret, '^'), '{', field('body', $.content), '}',
      optional(seq('~', '{', field('value', $.content), '}')),
    )),

    index_expr: $ => prec.right(100, seq(
      field('object', $.primary_expr),
      alias($._index_lbracket, '['),
      field('field', $._expr),
      repeat(seq(',', field('field', $._expr))),
      ']',
    )),
    last_index: _ => token(prec(2, 'last')),

    query_expr: $ => prec.left('query_expr', seq(
      field('object', $.primary_expr),
      field('op', choice('?', '.?')),
      field('query', $.primary_type),
    )),

    variadic: _ => token(prec(2, '...')),
    var_param_marker: _ => token(prec(3, 'var')),

    path_parent: _ => token(prec(3, '~~')),
    path_root: _ => token(prec(3, '/')),

    member_expr: $ => choice(
      prec.left(110, seq(
        field('object', choice($.primary_expr, $.member_expr)),
        alias($._member_dot, '.'),
        field('field', choice($.identifier, $.symbol, $.integer,
          $.path_wildcard, $.base_type)),
      )),
      prec.left('member', seq(
        field('object', choice($.primary_expr, $.member_expr)),
        alias($._member_dot, '.'),
        field('field', choice($.path_parent, $.path_root)),
      )),
    ),

    // S2.4.1v2: rooted `/.a` and relative `.a`. A path expression may only
    // begin a statement — never continue one — which is why a line-start `.`
    // is an S16.2.3 error unless it is the `.ident(` member-call form.
    // §7.15: the RELATIVE path is introduced by `\.` — `\` reads as the escape
    // character, saying "this dot is not member access, it introduces a path".
    // The rooted form `/.a` is unchanged. Retiring the bare-`.` relative path is
    // what lets `.ident` at a line start mean member access and nothing else,
    // which in turn widens the S16.2.4 carve-out to full leading-dot chains.
    // (`./` was the front-runner but collides with S10.5.1's postfix root step
    // `value./.name`; `\.` leaves that spelling untouched.)
    path_expr: $ => prec.right(choice(
      seq('/', '.', field('field', choice($.identifier, $.symbol,
        $.integer, $.path_wildcard, $.base_type, $.path_parent))),
      seq('\\.', field('field', choice($.identifier, $.symbol,
        $.integer, $.path_wildcard, $.base_type, $.path_parent, $.path_root))),
    )),

    current_parent_expr: _ => token(prec(4, '~~')),
    path_wildcard: _ => token(choice('**', '*')),

    _binary_eq_symbol_op: _ => token(choice('==', '!=')),
    _binary_eq_word_op: _ => token(choice('eq', 'ne')),
    _binary_word_relation_op: _ => token(choice('lt', 'le', 'ge', 'gt')),

    binary_expr: $ => choice(...binary_rules($, false)),

    current_expr: _ => token(choice('~#', '~')),
    current_error_expr: _ => prec(0, token('^')),

    _at: _ => token(prec(2, 'at')),
    _into: _ => token(prec(2, 'into')),
    _apply_kw: _ => token(prec(2, 'apply')),

    // §7.1: unary `!` is GONE from value expressions — `!true` used to mean
    // type complement and silently produced a type. `not` is the one logical
    // negation (S10.3.1 prefers words); `!` keeps its type-level roles.
    // §7.12: unary `+` is kept, so the whole arithmetic family `- + *` stays
    // dual-role at a line start rather than `+` becoming a lone exception.
    unary_expr: $ => prec.right(90, seq(
      field('operator', choice('-', '+', '*')),
      field('operand', $._expr),
    )),

    not_expr: $ => prec.right('logical_not', seq(
      'not', field('operand', $._expr),
    )),

    identifier: _ => {
      const alpha = /[^\x00-\x1F\s\p{Zs}0-9:;`"'@#.,|^&<=>+\-*/\\%?!~()\[\]{}\uFEFF\u2060\u200B\u2028\u2029]|\\u[0-9a-fA-F]{4}|\\u\{[0-9a-fA-F]+\}/;
      const alphanumeric = /[^\x00-\x1F\s\p{Zs}:;`"'@#.,|^&<=>+\-*/\\%?!~()\[\]{}\uFEFF\u2060\u200B\u2028\u2029]|\\u[0-9a-fA-F]{4}|\\u\{[0-9a-fA-F]+\}/;
      return token(seq(alpha, repeat(alphanumeric)));
    },

    // ============================= Functions ==============================

    parameter: $ => choice(
      seq(
        optional(field('var', $.var_param_marker)),
        field('name', choice($.identifier, $.symbol)),
        optional(field('optional', '?')),
        optional(seq(':', field('type', $._annotation_type))),
        optional(seq('=', field('default', $._expr))),
      ),
      field('variadic', $.variadic),
    ),

    named_argument: $ => seq(
      field('name', choice($.identifier, $.symbol)),
      ':',
      field('value', $._expr),
    ),

    // §7.6: `pub` is a uniform prefix MODIFIER — `pub let`, `pub fn`,
    // `pub type`. The old spelling replaced `let` outright (`pub x = 1`),
    // which made one keyword compose two different ways. `pub var` stays
    // illegal simply by the modifier not composing with `var`.
    fn_stam: $ => seq(
      optional(field('pub', 'pub')),
      field('kind', choice('fn', 'pn')),
      field('name', choice($.identifier, $.symbol)),
      '(', optional(field('declare', $.parameter)),
      repeat(seq(',', field('declare', $.parameter))), ')',
      optional(field('type', $.return_type)),
      field('body', $._body_block),
    ),

    fn_expr_stam: $ => seq(
      optional(field('pub', 'pub')),
      'fn', field('name', choice($.identifier, $.symbol)),
      '(', optional(seq(field('declare', $.parameter),
        repeat(seq(',', field('declare', $.parameter))))), ')',
      optional(field('type', $.return_type)),
      '=>', field('body', $._expr),
    ),

    // The arrow body is an ordinary expression, and since `{...}` is now
    // interior-differentiated (§5.9v3) that covers both block bodies
    // (`(x) => { let y = x + 1 y }`) and map results (`(x) => {a: x}`).
    fn_expr: $ => prec.right(choice(
      prec.dynamic(1, seq(
        '(', field('declare', $.parameter),
        repeat(seq(',', field('declare', $.parameter))), ')',
        optional(field('type', $.return_type)), '=>', field('body', $._expr),
      )),
      seq(
        '(', $._expr, repeat(seq(',', $._expr)), ')',
        optional(field('type', $.return_type)), '=>', field('body', $._expr),
      ),
      seq('(', ')', optional(field('type', $.return_type)),
        '=>', field('body', $._expr)),
    )),

    // ======================= Declarations and control =====================

    assign_expr: $ => prec.right(choice(
      seq(
        field('name', choice($.identifier, $.symbol)),
        optional(seq(':', field('type', $._annotation_type))),
        '=', field('as', $._expr),
      ),
      // §7.8: comma decomposition stands as designed — `let a, b = expr`
      // (positional) and `let a, b at expr` (named). Bracket patterns were
      // rejected; the first `=`/`at` position is the discriminator.
      seq(
        field('name', choice($.identifier, $.symbol)),
        repeat1(seq(',', field('name', choice($.identifier, $.symbol)))),
        field('decompose', choice('=', $._at)),
        field('as', $._expr),
      ),
    )),

    let_expr: $ => seq('let', field('declare', $.assign_expr)),

    let_stam: $ => seq(
      optional(field('pub', 'pub')), 'let',
      field('declare', $.assign_expr),
      repeat(seq(',', field('declare', $.assign_expr))),
    ),

    var_stam: $ => seq(
      'var', field('declare', $.assign_expr),
      repeat(seq(',', field('declare', $.assign_expr))),
    ),

    assign_stam: $ => seq(
      field('target', choice($.identifier, $.index_expr, $.member_expr)),
      '=', field('value', $._expr),
    ),

    // S16.6.1: ONE node, two spellings. There is no separate statement form —
    // the expression/statement distinction is semantic (S16.6.5), enforced in
    // build_ast on the fn/pn boundary.
    // S16.6.2: `(` immediately after the keyword COMMITS to the parenthesized
    // spelling, so a bare head may not begin with `(`. `_not_paren` is what
    // makes `if (a+b)*2 { … }` a loud error instead of a second parse.
    // S16.6.3: `else` is OPTIONAL in both spellings; an absent else yields
    // null in value position. A dangling `else` binds to the nearest `if`.
    if_expr: $ => choice($._if_closed, $._if_open),

    // Split by TAIL so §7.14 can classify without re-deriving it: the bare
    // spelling with no `else` ends on a brace that admits no postfix; every
    // other shape ends in an expression.
    _if_closed: $ => seq(
      'if', $._not_paren, field('cond', $._expr), field('then', $._braced),
    ),
    // The split reopens the dangling-else decision at the grammar level: in
    // `if (a) if b { } else …` the trailing `else` may close either `if`.
    // S16.6.3 binds it to the NEAREST one, which is this rule taking it, so
    // `_if_open` outranks `_if_closed`.
    _if_open: $ => prec.right(1, choice(
      seq('if', '(', field('cond', $._expr), ')', field('then', $._expr),
        optional(seq('else', field('else', $._expr)))),
      seq('if', $._not_paren, field('cond', $._expr), field('then', $._braced),
        'else', field('else', $._expr)),
    )),

    // `while` is procedural-only and always discards its body value, so its
    // body is structurally a block — a map there would be dead (§5.9v3).
    while_expr: $ => prec.right(seq(
      'while',
      choice(
        seq('(', field('cond', $._expr), ')', field('body', $._body_block)),
        seq($._not_paren, field('cond', $._expr), field('body', $._body_block)),
      ),
    )),

    match_expr: $ => seq(
      'match', field('scrutinee', $._expr),
      '{', repeat1(choice($.match_arm, $.match_default)), '}',
    ),
    // §5.5: match keeps its single braced form — the braces delimit an arm
    // LIST, not a body, so no parenthesized spelling exists.
    match_arm: $ => prec.right(seq(
      'case', field('pattern', $._annotation_type),
      choice(
        seq(':', field('body', $._expr)),
        field('body', $._body_block),
      ),
    )),
    match_default: $ => prec.right(seq(
      'default',
      choice(
        seq(':', field('body', $._expr)),
        field('body', $._body_block),
      ),
    )),

    loop_expr: $ => choice(
      seq(
        field('name', $.identifier),
        optional(field('optional', '?')),
        field('op', choice('in', $._at)),
        field('as', $._expr),
        optional(seq('on', field('on', $._expr))),
      ),
      seq(
        field('index', $.identifier),
        optional(seq(':', field('index_type', $.identifier))),
        ',', field('name', $.identifier),
        optional(field('optional', '?')),
        'in', field('as', $._expr),
        optional(seq('on', field('on', $._expr))),
      ),
    ),

    for_let_clause: $ => seq(
      'let', field('name', $.identifier), '=', field('value', $._expr),
    ),
    for_where_clause: $ => prec.dynamic(10, seq('where', field('cond', $._expr))),
    order_spec: $ => seq(
      field('expr', $._expr), optional(field('dir', choice('asc', 'desc'))),
    ),
    for_order_clause: $ => seq(
      'order', 'by', field('spec', $.order_spec),
      repeat(seq(',', field('spec', $.order_spec))),
    ),
    group_key_spec: $ => seq(
      field('key', $.primary_expr), optional(seq('as', field('alias', $.identifier))),
    ),
    for_group_clause: $ => prec.dynamic(10, seq(
      'group', 'by', field('spec', $.group_key_spec),
      repeat(seq(',', field('spec', $.group_key_spec))),
      $._into, field('name', $.identifier),
    )),
    for_limit_clause: $ => seq(
      'limit', optional(field('last', $.last_index)), field('count', $._expr),
    ),
    for_offset_clause: $ => seq('offset', field('count', $._expr)),

    for_clauses: $ => repeat1(choice(
      field('where', $.for_where_clause),
      field('group', $.for_group_clause),
      field('order', $.for_order_clause),
      field('limit', $.for_limit_clause),
      field('offset', $.for_offset_clause),
    )),

    _loop_head: $ => seq(
      field('declare', $.loop_expr),
      repeat(seq(',', field('declare', $.loop_expr))),
      repeat(seq(',', field('let', $.for_let_clause))),
      optional($.for_clauses),
    ),

    // `for` needs no `_not_paren` guard: a loop declaration always begins with
    // an identifier, so `(` after `for` is unambiguously the paren spelling.
    for_expr: $ => choice($._for_closed, $._for_open),
    _for_closed: $ => seq('for', $._loop_head, field('then', $._braced)),
    _for_open: $ => prec.right(seq('for', '(', $._loop_head, ')',
      field('then', $._expr))),

    break_stam: _ => 'break',
    continue_stam: _ => 'continue',

    // S16.2.5: `return` takes a following start-token line as its value, so
    // `return` ⏎ `42` means `return 42`. A bare return is `return` followed by
    // a separator or the closing brace. The JS restricted-production trap is
    // fixed by inversion rather than by a special rule.
    return_stam: $ => prec.right(seq(
      'return', optional(field('value', $._expr)),
    )),

    // §7.7: the fused `apply;` token is retired. Bare `apply` is a keyword
    // statement; `apply(...)` stays an ordinary call. The same-line `(` test
    // that separates them is exactly the S16.2.5 shape already used by
    // `return`, so no fused lexeme is needed.
    apply_stam: $ => seq($._apply_kw, $._not_paren),

    // ========================= View declarations ==========================

    view_stam: $ => seq(
      field('kind', token(prec(1, choice('view', 'edit')))),
      optional(seq(field('name', $.identifier), ':')),
      field('pattern', $.view_pattern),
      optional(seq(
        '(', optional(seq(field('declare', $.parameter),
          repeat(seq(',', field('declare', $.parameter))))), ')',
        optional(field('type', $.return_type)),
      )),
      optional(field('state', $.state_decl)),
      field('body', $._body_block),
      repeat(field('handler', $.event_handler)),
    ),

    state_decl: $ => seq('state', $.state_entry, repeat(seq(',', $.state_entry))),
    state_entry: $ => seq(field('name', $.identifier), ':', field('value', $._expr)),

    event_handler: $ => seq(
      'on', field('event', $.identifier),
      '(', optional(field('declare', $.parameter)), ')',
      field('body', $._body_block),
    ),

    _view_pattern_primary: $ => choice($.element_type, $.identifier, $.base_type),
    view_pattern: $ => choice(
      $._view_pattern_primary,
      alias($.view_pattern_union, $.binary_type),
    ),
    view_pattern_union: $ => prec.left('set_union', seq(
      field('left', $._view_pattern_primary),
      field('operator', '|'),
      field('right', choice($._view_pattern_primary,
        alias($.view_pattern_union, $.binary_type))),
    )),

    // ============================ Type language ===========================

    _base_type_kw: _ => token(prec(1, choice(
      'null', 'any', 'bool', 'int64', 'int', 'float', 'f64', 'complex',
      'decimal', 'integer', 'number', 'datetime', 'date', 'time', 'binary',
      'range', 'list', 'array', 'map', 'element', 'entity', 'object',
      'function', 'error', 'string', 'symbol',
      'i8', 'i16', 'i32', 'i64', 'u8', 'u16', 'u32', 'u64', 'f16', 'f32',
    ))),
    base_type: $ => choice($._base_type_kw, 'type'),

    occurrence: $ => choice('?', '+', '*', $.occurrence_count),
    // The occurrence bracket takes the same same-line guard as an index: `[` is
    // dual-role in TYPE space too (`int[3]` is an occurrence, `[int]` an array
    // type), so `type T = int` ⏎ `[3]` must be the S16.2.3 error rather than a
    // silent continuation. This is O3's rule applied on the grammar side — type
    // space shares the S16.2.2 continuation set instead of keeping its own.
    occurrence_count: $ => prec(2, choice(
      seq(alias($._index_lbracket, '['), ']'),
      seq(alias($._index_lbracket, '['), $.integer, ']'),
      seq(alias($._index_lbracket, '['), $.integer, ',', $.integer, ']'),
      seq(alias($._index_lbracket, '['), $.integer, '+', ']'),
    )),

    return_occurrence_type: $ => seq(
      field('operand', choice($.base_type, $.identifier)),
      optional(field('operator', $.occurrence)),
    ),
    return_type_pattern: $ => prec.left(seq(
      field('type', $.return_occurrence_type),
      repeat(seq(choice('|', '&', '!'), field('type', $.return_occurrence_type))),
    )),
    return_type: $ => prec.right(seq(
      field('ok', $.return_type_pattern),
      optional(seq('^', optional(field('error', $.return_type_pattern)))),
    )),

    list_type: $ => prec.dynamic(2, seq(
      '(', seq($._type_pattern, repeat(seq(',', $._type_pattern))), ')',
    )),
    array_type: $ => seq('[', comma_sep($._type_pattern), ']'),
    map_type_item: $ => seq(
      field('name', $._field_name),
      optional(field('optional', '?')),  // §7.22: optional FIELD, as in object types
      ':', field('as', $._type_pattern),
    ),
    map_type: $ => seq('{',
      optional(seq($.map_type_item, repeat(seq(',', $.map_type_item)))), '}',
    ),

    pattern_attr_type: $ => prec(1, seq(
      field('name', $._field_name),
      // §7.22: `a?: T` marks the FIELD optional (it may be absent);
      // `a: T?` makes the VALUE nullable. The two are different claims.
      optional(field('optional', '?')),
      ':', field('as', $._type_pattern),
      optional(seq('=', field('default', $._non_null_literal))),
    )),
    content_type: $ => seq($._type_pattern, repeat(seq(',', $._type_pattern))),

    // A namespace-qualified tag is legal in an element VALUE (S2.4.3v2), so an
    // element TYPE must admit one too — `type T = <soap.Fault …>`.
    element_type: $ => seq('<', choice($.dotted_name, $.identifier), choice(
      seq(alias($.pattern_attr_type, $.attr),
        repeat(seq(',', alias($.pattern_attr_type, $.attr))),
        optional(seq(',', $.content_type))),
      optional($.content_type),
    ), '>'),

    fn_param: $ => seq(
      field('name', $.identifier), seq(':', field('type', $._type_pattern)),
    ),
    fn_type: $ => seq(
      'fn',
      optional(seq('(', optional(field('declare', $.fn_param)),
        repeat(seq(',', field('declare', $.fn_param))), ')')),
      field('type', $.return_type),
    ),

    range_type: $ => prec.left('range_to', seq(
      field('start', $._non_null_literal), 'to', field('end', $._non_null_literal),
    )),

    primary_type: $ => choice(
      $.range_type,
      $._non_null_literal,
      $.base_type,
      $.identifier,
      $.list_type,
      $.array_type,
      $.map_type,
      $.element_type,
      $.pattern_island,
    ),

    occurrence_type: $ => prec.dynamic(1, prec.right(seq(
      field('operand', $.primary_type), field('operator', $.occurrence),
    ))),
    nullable_array_type: $ => prec.dynamic(2, prec.right(seq(
      field('operand', $.occurrence_type), field('operator', $.occurrence_count),
    ))),
    negation_type: $ => prec.right(seq('!', field('operand', $.primary_type))),

    unary_type: $ => prec.right(choice(
      $.nullable_array_type,
      $.occurrence_type,
      $.negation_type,
      $.primary_type,
    )),
    binary_type: $ => choice(...type_operators($._type_pattern)),

    _type_pattern: $ => choice($.unary_type, $.binary_type, $.fn_type),

    _annotation_type: $ => choice($._type_pattern, $.constrained_type),

    constrained_type: $ => prec.right(seq(
      field('base', $._type_pattern), 'that', field('constraint', $._expr),
    )),

    attr_type: $ => prec(1, seq(
      field('name', $._field_name),
      optional(field('optional', '?')),  // §7.22: optional FIELD
      ':', field('as', $._annotation_type),
      optional(seq('=', field('default', $._element_expr))),
    )),

    type_assign: $ => seq(
      field('name', choice($.identifier, $.symbol)), '=',
      field('as', $._annotation_type),
    ),
    type_stam: $ => seq(
      optional(field('pub', 'pub')), 'type',
      field('declare', alias($.type_assign, $.assign_expr)),
      repeat(seq(',', field('declare', alias($.type_assign, $.assign_expr)))),
    ),

    that_constraint: $ => prec.right(seq('that', field('constraint', $._expr))),

    // §7.11: `;` has left the object type too. Fields, the object-level
    // constraint, and methods are ONE comma list. After a comma, `that` cannot
    // start a field (fields need `name:`), so the separator itself tells the
    // object-level constraint from a field-level `z: string that …` — no new
    // keyword is needed. The comma before a method is load-bearing rather than
    // stylistic: fn TYPES exist, so a bare `fn` could otherwise continue the
    // preceding field's type.
    object_type: $ => seq(
      optional(field('pub', 'pub')),
      'type', field('name', choice($.identifier, $.symbol)),
      optional(seq(':', field('base', choice($.identifier, $.symbol)))),
      '{',
      optional(comma_sep1(choice(
        alias($.attr_type, $.attr),
        $.that_constraint,
        $.fn_stam,
        $.fn_expr_stam,
        $._type_pattern,
      ))),
      '}',
    ),

    // ==================== String / symbol pattern islands =================

    _pattern_tag: _ => token(choice('\\symbol(', '\\(')),
    pattern_island: $ => seq(
      field('tag', $._pattern_tag), field('body', $._pattern_expr), ')',
    ),

    pattern_char_class: _ => choice('...', 'd', 'w', 's', 'a', '.'),
    _pattern_primary_type: $ => choice(
      $.range_type, $._non_null_literal, $.identifier, $.pattern_char_class,
    ),
    pattern_occurrence_type: $ => prec.right(seq(
      field('operand', $._pattern_primary_type), field('operator', $.occurrence),
    )),
    pattern_negation_type: $ => prec.right(seq(
      '!', field('operand', $._pattern_primary_type),
    )),
    grouped_type: $ => prec.right(seq(
      optional('!'), '(', $._pattern_expr, ')',
      optional(field('occurrence', $.occurrence)),
    )),
    concat_type: $ => prec.left(1, seq(
      choice($.pattern_unary_type, $.grouped_type),
      repeat1(choice($.pattern_unary_type, $.grouped_type)),
    )),
    string_binary_type: $ => choice(...type_operators($._pattern_expr)),
    pattern_unary_type: $ => prec.right(choice(
      $.pattern_occurrence_type,
      $.pattern_negation_type,
      $._pattern_primary_type,
    )),
    _pattern_expr: $ => choice(
      $.pattern_unary_type,
      $.concat_type,
      alias($.string_binary_type, $.binary_type),
      $.grouped_type,
    ),

    // ============================== Imports ===============================

    relative_name: $ => repeat1(seq(choice('.', '\\'), $.identifier)),
    absolute_name: $ => seq(
      $.identifier, repeat(seq(choice('.', '\\'), $.identifier)),
    ),
    import_module: $ => choice(
      field('module', choice($.absolute_name, $.relative_name, $.symbol)),
      seq(field('alias', $.identifier), ':',
        field('module', choice($.absolute_name, $.relative_name, $.symbol))),
    ),
    _import_stam: $ => seq(
      'import', $.import_module, repeat(seq(',', $.import_module)),
    ),
  },
});

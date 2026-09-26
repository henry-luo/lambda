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
 * KEYWORDS (S16.10) are tree-sitter RESERVED words (`reserved` below), which
 * need tree-sitter CLI 0.25 or later and make this parser ABI 15.
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

// S16.10.1v2 (Design_Syntax Appendix K.1): a word that can BEGIN a construct
// never names a binding, so the grammar reserves it (`reserved` below). These
// words stay legal DATA names (S16.10.2, S16.10.3) through `_keyword_name`,
// exactly as the C parser's `token_is_name_word` admits them.
const KEYWORD_NAMES = [
  'let', 'pub', 'var', 'type', 'fn', 'pn', 'view', 'edit', 'state',
  'if', 'match', 'for', 'while', 'break', 'continue', 'return', 'raise',
  'import', 'apply', 'last', 'put', 'del', 'commit', 'rollback', 'open',
];
// Reserved as well, but a data name in neither parser: the prefix operator
// `not` and the named values.
const RESERVED_VALUE_WORDS = ['not', 'true', 'false', 'inf', 'nan'];

// S2.4.3v2: a namespace-qualified name is maximal, so `<svg .rect>` keeps the
// tag `svg.rect` while `<svg, .rect>` (§7.11) is tag + path child.
function qualified_name($, precedence) {
  return prec.left(precedence, seq(
    $._data_name,
    repeat1(prec.left(precedence, seq(
      // Either dot may arrive here. In a position where a member expression is
      // also possible — an element interior, where `xml` could equally begin
      // content — the scanner emits the guarded `_member_dot` and the internal
      // high-precedence dot is never produced, so a namespaced NAME must accept
      // both spellings or `<div xml.lang: "en">` cannot parse.
      choice(token(prec(precedence, '.')), alias($._member_dot, '.')),
      $._data_name,
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
// operand. Operators that can only ever continue (`|> |: | & % > == != <= >=`,
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
  const mk = (operator, precedence, assoc, right = operand) => wrap(assoc)(precedence, seq(
    field('left', operand), field('operator', operator), field('right', right),
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
    // S10.1.6: `|:` is the filter stage; `that` is the single-value proviso
    // (S10.1.5v3). Both share the pipe tier, left-associative.
    mk('|:', 'pipe', 'left'),
    mk('that', 'pipe', 'left'),
    mk(alias($._bin_amp, '&'), 'set_intersect', 'left'),
    // §7.1 removed unary `!` from value expressions, so infix `!` (set
    // exclusion) is unguarded: it can only continue.
    mk('!', 'set_exclude', 'left'),
    // S11.1.6v2: the right side of `is` is a boundary TYPE, as a `match` arm
    // is, so `x is int?` and `x is int[]` keep their suffixes instead of
    // reading as a query and a keyless index. As in the C parser's type slot,
    // every suffix binds to that type: a same-line `+` or `*` after it is an
    // occurrence, never arithmetic on the `is` result.
    mk('is', 'is_in', 'left', $._type_pattern),
    mk('<:', 'is_in', 'left'),
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

function type_operators($, type_expr) {
  return [
    ['|', 'set_union'],
    // Where a type ends an expression (`x is int & number`) the scanner offers
    // the guarded `&` first, so the type takes it too. A line-start `&` stays
    // the plain token: in type space it can only continue, as in C.
    [choice('&', alias($._bin_amp, '&')), 'set_intersect'],
    ['!', 'set_exclude'],
  ].map(([operator, precedence]) => prec.left(precedence, seq(
    field('left', type_expr),
    field('operator', operator),
    field('right', type_expr),
  )));
}

// S11.1.1v3 / S11.1.6v2: a type's suffix chain, the grammar's counterpart of
// the C parser's `apply_occurrence`. A lone occurrence takes any suffix
// `single` admits. Array suffixes compose left to right after a `?` or another
// array suffix (`int?[]`, `int[2][3]`), and a `?` after an array suffix is the
// nullable array (`int[]?`). Nothing else chains: `int??` and `int[]??` are
// errors, and so is `int+[]` -- the Type_Pattern §1.3 no-chaining rule, under
// which an array of runs is grouped, `(int+)[]`. The chain is instantiated per
// operand; `names` lists one instantiation's rules, each shown in the tree as
// the general type's node.
const TYPE_CHAIN = {
  occurrence: 'occurrence_type', array: 'nullable_array_type',
  nullable: 'optional_array_type', array_head: '_array_occurrence_type',
  nullable_head: '_nullable_occurrence_type',
};
const RETURN_CHAIN = {
  occurrence: '_return_occurrence_type', array: '_return_nullable_array_type',
  nullable: '_return_optional_array_type', array_head: '_return_array_head',
  nullable_head: '_return_nullable_head',
};

function suffix_chain(names, head, single) {
  const shown = {
    occurrence: 'occurrence_type', array: 'nullable_array_type',
    nullable: 'optional_array_type', array_head: 'occurrence_type',
    nullable_head: 'occurrence_type',
  };
  const link = ($, key) => names[key] === shown[key]
    ? $[names[key]] : alias($[names[key]], $[shown[key]]);
  return {
    [names.occurrence]: $ => prec.dynamic(1, prec.right(seq(
      field('operand', head($)), field('operator', single($)),
    ))),
    [names.array]: $ => prec.dynamic(2, prec.right(seq(
      field('operand', choice(link($, 'nullable_head'), link($, 'array_head'),
        link($, 'array'), link($, 'nullable'))),
      field('operator', $.array_count),
    ))),
    [names.nullable]: $ => prec.dynamic(2, prec.right(seq(
      field('operand', choice(link($, 'array_head'), link($, 'array'))),
      field('operator', '?'),
    ))),
    // The two heads a chain continues from, each the occurrence type it is,
    // spelled as its own symbol so no other lone suffix can continue.
    [names.array_head]: $ => seq(field('operand', head($)),
      field('operator', alias($._array_occurrence, $.occurrence))),
    [names.nullable_head]: $ => seq(field('operand', head($)),
      field('operator', alias($._nullable_occurrence, $.occurrence))),
  };
}

// Every callable signature's parameter list, as C's `parse_parameter_items`
// reads it: named parameters, then at most one rest parameter `...`, which
// comes last, arrows included (S16.9.7); `,` is a strict separator
// (S16.1.2v2), so `(, a)` and `(..., a)` are errors. A procedure arrow passes
// `_value_parameter`, the named parameter without `var` (S16.6.7v2).
function parameter_list($, named_parameter = $._named_parameter) {
  const named = field('declare', alias(named_parameter, $.parameter));
  const rest = field('declare', alias($._rest_parameter, $.parameter));
  return seq('(', optional(choice(
    seq(named, repeat(seq(',', named)), optional(seq(',', rest))),
    rest,
  )), ')');
}

// A named parameter after its optional `var` marker: name, `?`, type, default.
function parameter_body($) {
  return [
    field('name', choice($.identifier, $.symbol)),
    optional(field('optional', '?')),
    optional(seq(':', field('type', $._parameter_annotation_type))),
    optional(seq('=', field('default', $._expr))),
  ];
}

// The two declaration shapes, parameterized by what may name them: a binding
// name at statement level, a data name for an object type's method.
function fn_declaration($, name) {
  return seq(
    optional(field('pub', 'pub')),
    field('kind', choice('fn', 'pn', 'function')),
    field('name', name),
    parameter_list($),
    optional(field('type', $.return_type)),
    field('body', $._body_block),
  );
}

function fn_expr_declaration($, name) {
  return seq(
    optional(field('pub', 'pub')),
    field('kind', choice('fn', 'function')), field('name', name),
    parameter_list($),
    optional(field('type', $.return_type)),
    '=>', field('body', $._expr_body),
  );
}

// A return contract: alternatives joined by `|`, `&` or `!`, then an optional
// `^` error arm (`T^` any error, `T^E` a named one).
const return_contract = pattern => $ => prec.right(seq(
  field('ok', pattern($)),
  optional(seq('^', optional(field('error', pattern($))))),
));
const return_alternatives = atom => $ => prec.left(seq(
  field('type', atom($)),
  repeat(seq(choice('|', '&', '!'), field('type', atom($)))),
));

module.exports = grammar({
  name: "lambda",

  extras: $ => [/\s/, $.comment],

  word: $ => $.identifier,

  // S16.10.1v2. Keyword extraction (`word`) lexes a keyword as an identifier
  // wherever the parse state has no action for it, so without this set every
  // binding position silently took keywords (`let if = 1`; `let a = let b = 2`
  // as `let a = let`) while a data name that could also start a statement was
  // refused (`{while: 1}`). A reserved word lexes as its keyword wherever an
  // identifier is expected, so it is an error unless the state takes that
  // keyword: its own construct, or `_keyword_name` in a data-name position.
  // An entry must be the very token the keyword lexer yields for its word,
  // which is why no reserved word has a second, differently spelled token.
  // `lambda` is absent: it is an identifier in both parsers, and its
  // S16.10.1v2 bar is the compile-time E201.
  reserved: {
    global: $ => [
      ...KEYWORD_NAMES, ...RESERVED_VALUE_WORDS, $._base_type_kw,
    ],
  },

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
    // PTH40: `&` is dual-role now (infix set intersection, prefix address-of).
    $._bin_amp,
    $._call_lparen,
    $._index_lbracket,
    // S11.1.6v2: zero-width, in front of the `{` of a counted occurrence. Like
    // the index bracket it is same-line only -- a line-start `{` is a new
    // statement, never a type suffix -- and it also needs the brace bound tight.
    $._occurrence_lbrace,
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
    // S2.4.1v2: zero-width, emitted after a bare path root `/` or `\` only when
    // a step or the end of the path follows, so `/b` (the retired `/a`
    // spelling) is an error rather than `/` plus a juxtaposed statement `b`.
    $._root_boundary,
    // S11.1.5v2: zero-width, in front of a signature's return type, which must
    // start on the `)` line; a name opening the next line gets no token at all.
    $._fn_return,
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
    // The same fork for a keyword-spelled field (`type?: T` against the
    // content type `type?`), whose name reaches `_field_name` through
    // `_keyword_name`. A keyword that heads a statement needs no fork: no
    // statement continues its keyword with `:`, so in `{while: 1}` one token
    // of lookahead settles map key against block.
    [$._keyword_name, $.base_type],
    // Inside an element, `last.x` opens a dotted attribute name
    // (`<div last.x: 1>`) or is the content value `last` and a member step;
    // the `:` two tokens later decides.
    [$._keyword_name, $.last_index],
    // S2.5.1v2: `(x, y)` is a list and `(x, y) => …` an arrow head, so a bare
    // name in a group is a parameter and an item at once until the `=>` (or
    // its absence) decides. Parsing them side by side, rather than reading
    // the head as expressions, keeps the head a plain parameter list. `...`
    // forks the same way: an item, or the arrow's rest parameter.
    [$._named_parameter, $.primary_expr],
    [$._rest_parameter, $.primary_expr],
  ],

  supertypes: $ => [],

  inline: $ => [
    $._non_null_literal,
    $._parenthesized_expr,
    $._list_item,
    $._number,
    $._key,
    $._data_name,
    // S16.6.6: inlined so an expression body is never a reduction point of its
    // own. As a real nonterminal it forced a choice between reducing
    // `_expr_body` and continuing a trailing binary operator (`... => x > y`).
    $._expr_body,
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
      'unary',
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
      // PTH68v3: the transaction block ends in a braced body, like `while`.
      $.open_stam,
      // S16.6.7v2: a procedure arrow ends in its `pn` body's `}`.
      $.proc_expr,
    ),

    _open_stam: $ => choice(
      $.let_stam,
      $.var_stam,
      $.fn_expr_stam,
      $.type_stam,
      $.assign_stam,
      $.return_stam,
      // PTH60v3/PTH62: the Tier-3 statements. `put`/`del` end in a greedy
      // clause list, `commit`/`rollback` in a bare word.
      $.put_stam,
      $.del_stam,
      $.commit_stam,
      $.rollback_stam,
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

    // Four separate word tokens rather than one fused token, so each is a
    // keyword the `reserved` set can name.
    named_value: _ => choice(
      'true', 'false', 'inf', 'nan', 'decimal.inf', 'decimal.nan',
    ),

    _non_null_literal: $ => choice(
      $._number, $.string, $.symbol, $.datetime, $.binary, $.named_value,
    ),

    // ============================ Containers ==============================

    // Names, not types: a field/attribute may be spelled with a keyword or a
    // base-type word (`type: string`, `string: int`).
    _field_name: $ => $._data_name,

    _key: $ => choice($._data_name, '*'),

    // S16.10.2 / S16.10.3: a DATA name -- map key, field, attribute, element
    // tag, member step, fragment, method, named argument -- may be spelled
    // with a keyword or base-type word, as the C parser's `token_is_key`
    // admits; `not` and the named values are the reserved words it refuses.
    _data_name: $ => choice($.identifier, $.symbol,
      alias($._keyword_name, $.identifier)),

    // The SAME tokens the constructs use, deliberately: a separate,
    // higher-precedence token would win in the lexer and the construct heads
    // could never match. Which reading applies is a parse-state decision, and
    // where a statement could also start (`{while: 1}`) the `:` one token
    // later settles it.
    _keyword_name: $ => choice(...KEYWORD_NAMES, $._base_type_kw),

    // S16.8.9: a bracketed key is evaluated, while a bare name remains a
    // literal attribute name. Keeping the computed form separate prevents
    // `_key` from admitting expressions anywhere other than keyed literals.
    computed_key: $ => seq('[', field('key', $._expr), ']'),

    map_item: $ => seq(
      field('name', choice($._key, $.computed_key)),
      ':', field('as', $._expr),
    ),

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

    attr_name: $ => choice(alias($._attr_dotted_name, $.dotted_name),
      $._key, $.computed_key),
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
      field('tag', choice($.dotted_name, $._data_name)),
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

    // S2.5.1v2 / S2.5.5v2: a parenthesized comma list is a list literal. A
    // group of one item is that item (`(x)` ≡ `x`) and `()` is the empty list,
    // `null`. S2.5.4: a `let` item declares and contributes nothing, and each
    // binding takes its own `let` (Design_Syntax §7.18), so `(let a = 1, b, c)`
    // has one reading. `,` is a strict separator (S16.1.2v2).
    _parenthesized_expr: $ => choice(seq('(', $._list_item, ')'), $.list),
    list: $ => seq('(',
      optional(seq($._list_item, repeat1(seq(',', $._list_item)))), ')'),
    _list_item: $ => choice($.let_expr, $._expr),

    _expr: $ => choice(
      $.primary_expr,
      $.unary_expr,
      $.address_of_expr,
      $.not_expr,
      $.binary_expr,
      $.if_expr,
      $.while_expr,
      $.match_expr,
      $.for_expr,
      $.raise_expr,
      $.proc_expr,
    ),

    // S16.6.6: an unbraced body is an expression position, so `return`,
    // `break` and `continue` are barred there. They are reserved words with no
    // action in an expression, so they are errors here with no guard; `raise`
    // is an expression and stays valid. A BRACED body in any of these positions
    // is the statement spelling and is unaffected.
    _expr_body: $ => $._expr,

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
      $.char_pattern_island,
      $.identifier,
      $.index_expr,
      $.path_expr,
      $.member_expr,
      $.handler_expr,
      $.propagate_expr,
      $.force_expr,
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
        alias('apply', $.identifier),
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
    // The same `last` word token a data name spells (`_keyword_name`).
    last_index: _ => 'last',

    // PTH32/PTH33: the force step. `#` is a PURE continuation token -- it has
    // no prefix reading, so unlike `(`/`[`/`.`/`^` it needs no `_join` guard
    // and a line may begin `#name` (S16.2.2v3). The fragment step must abut the
    // `#`, which `token(seq(...))` enforces: `p# name` is two expressions.
    force_expr: $ => prec.left(110, seq(
      field('operand', $.primary_expr),
      '#',
      optional(field('fragment', choice($._data_name, $.integer))),
    )),

    // PTH40: prefix `&` is address-of. `&` is also infix set intersection, so
    // it is DUAL-ROLE (S16.2.3v3) and a line beginning `&x` after an open-tail
    // statement is an error repaired by `;`.
    address_of_expr: $ => prec.right('unary', seq(
      '&', field('operand', $._expr),
    )),

    query_expr: $ => prec.left('query_expr', seq(
      field('object', $.primary_expr),
      field('op', choice('?', '.?')),
      field('query', $.primary_type),
    )),

    variadic: _ => token(prec(2, '...')),

    path_parent: _ => token(prec(3, '~~')),
    path_root: _ => token(prec(3, '/')),

    member_expr: $ => choice(
      prec.left(110, seq(
        field('object', choice($.primary_expr, $.member_expr)),
        alias($._member_dot, '.'),
        field('field', choice($._data_name, $.integer, $.path_wildcard)),
      )),
      prec.left('member', seq(
        field('object', choice($.primary_expr, $.member_expr)),
        alias($._member_dot, '.'),
        field('field', choice($.path_parent, $.path_root)),
      )),
    ),

    // S2.4.1v2: rooted `/.a` and relative `\.a`. A path expression may only
    // begin a statement — never continue one — which is why a line-start `.`
    // is an S16.2.3 error unless it is the `.ident(` member-call form.
    // §7.15: the RELATIVE path is rooted at `\`, as the logical one is at `/`.
    // Retiring the bare-`.` relative path is what lets `.ident` at a line start
    // mean member access and nothing else, which in turn widens the S16.2.4
    // carve-out to full leading-dot chains. (`./` was the front-runner but
    // collides with S10.5.1's postfix root step `value./.name`; `\` leaves that
    // spelling untouched.)
    // S2.4.1v2 + S2.4.2v5: the roots `/` and `\` are complete paths, and every
    // step is an ordinary member/index step on the path -- `\.a` is `\` then
    // `.a`, `/[1]` and `\[1]` are the paths `/.1` and `\.1`, and `.[` is an
    // error anywhere. `_root_boundary` rejects anything else touching the
    // root (`/b`). This mirrors the C parser's `parse_path_slot`.
    path_expr: $ => seq(choice('/', '\\'), $._root_boundary),

    current_parent_expr: _ => token(prec(4, '~~')),
    path_wildcard: _ => token(choice('**', '*')),

    // PTH45v2: `===` is reference equality. Longest match first, so `a === b`
    // never lexes as `a == (= b)`.
    _binary_eq_symbol_op: _ => token(choice('===', '==', '!=')),
    _binary_eq_word_op: _ => token(choice('eq', 'ne')),
    _binary_word_relation_op: _ => token(choice('lt', 'le', 'ge', 'gt')),

    binary_expr: $ => choice(...binary_rules($, false)),

    // PTH47: the current key/index accessor is `~key`; `~#` is retired so that
    // `#` means force everywhere (S1.7). `~key` is a fused token -- `~ key`
    // with a space stays two tokens.
    current_expr: _ => token(choice('~key', '~')),
    current_error_expr: _ => prec(0, token('^')),

    _at: _ => token(prec(2, 'at')),
    _into: _ => token(prec(2, 'into')),

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

    // A signature's lists go through `parameter_list`, which places the rest
    // parameter; an event handler takes one parameter of either kind.
    parameter: $ => choice($._named_parameter, $._rest_parameter),
    // `primary_expr`'s precedence, so that a bare name or `...` in a group is
    // a real GLR fork between parameter and item (see `conflicts`), not a
    // reduction precedence settles before `=>` is seen.
    // The parameter's own body stays inline (`parameter_body`), not a hidden
    // rule of its own: the fork against `primary_expr` above is declared on
    // `_named_parameter`, and an extra rule level would move the name out of it.
    _named_parameter: $ => prec(50, seq(
      optional(field('var', alias('var', $.var_param_marker))),
      ...parameter_body($),
    )),
    // S16.6.7v2: a procedure arrow is only called through a value, which binds
    // no `var` parameter (S12.3.2), so its list takes none.
    _value_parameter: $ => prec(50, seq(...parameter_body($))),
    _rest_parameter: $ => prec(50, field('variadic', $.variadic)),

    // Spelled like a data name, as C's `token_is_key` reads it: `f(let: 1)`,
    // `f(type: 1)`.
    named_argument: $ => seq(
      field('name', $._data_name),
      ':',
      field('value', $._expr),
    ),

    // §7.6: `pub` is a uniform prefix MODIFIER — `pub let`, `pub fn`,
    // `pub type`. The old spelling replaced `let` outright (`pub x = 1`),
    // which made one keyword compose two different ways. `pub var` stays
    // illegal simply by the modifier not composing with `var`.
    // S12.1.4v2: `function` declares a colour-polymorphic `fn` — pure iff its
    // `function`-typed arguments are.
    fn_stam: $ => fn_declaration($, choice($.identifier, $.symbol)),
    fn_expr_stam: $ => fn_expr_declaration($, choice($.identifier, $.symbol)),
    // S16.10.2: a method's name is a data name, reached only through a
    // receiver (`x.state()`), so inside an object type it admits keywords.
    _method_stam: $ => fn_declaration($, $._data_name),
    _method_expr_stam: $ => fn_expr_declaration($, $._data_name),

    // The arrow body is an ordinary expression, and since `{...}` is now
    // interior-differentiated (§5.9v3) that covers both block bodies
    // (`(x) => { let y = x + 1 y }`) and map results (`(x) => {a: x}`).
    // The head is a parameter list and nothing else, as in C's
    // `arrow_head_candidate`: `(x, y)` reads equally as parameters and as a
    // list (see `conflicts`), and only `=>` or a return type settles it.
    fn_expr: $ => prec.right(seq(
      parameter_list($),
      optional(field('type', $.return_type)), '=>', field('body', $._expr_body),
    )),

    // S16.6.7v2: the procedure arrow, a nested `pn` without its name. The arrow
    // makes it anonymous; its body is the braced block every `pn` takes
    // (S16.4.3), never an arrow's expression body. It is no primary: its `}`
    // closes a `pn` body, which admits no postfix (S16.1.3v2), so a statement
    // it ends is a closed tail.
    proc_expr: $ => seq(
      field('kind', 'pn'), parameter_list($, $._value_parameter),
      optional(field('type', $.return_type)), '=>', field('body', $._body_block),
    ),

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

    // Tier 3 (PTH55-PTH80): every document write is one of these statements;
    // `=` never writes a document. `before`, `after` and `into` are CLAUSE
    // words inside the statement, like `in` in a `for` header, so they stay
    // bindable (S16.10.1) -- which is why they are not lexed as keywords.
    put_stam: $ => seq(
      'put', field('clause', $._put_clause),
      repeat(seq(',', field('clause', $._put_clause))),
    ),

    _put_clause: $ => choice(
      seq(field('target', $._expr), '=', field('value', $._expr)),
      seq(field('value', $._expr), choice('before', 'after', 'into'),
          field('target', $._expr)),
    ),

    del_stam: $ => seq(
      'del', field('target', $._expr),
      repeat(seq(',', field('target', $._expr))),
    ),

    commit_stam: _ => 'commit',
    rollback_stam: _ => 'rollback',

    // PTH68v3/PTH75v3: the block is a bounded transaction and the optional
    // alias is a reference with `#` implied -- the `=` is a binding introducer,
    // as in `let`, not an assignment.
    open_stam: $ => seq(
      'open',
      optional(seq(field('alias', $.identifier), '=')),
      field('target', $._expr),
      '{', field('body', $.content), '}',
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
      seq('if', '(', field('cond', $._expr), ')', field('then', $._expr_body),
        optional(seq('else', field('else', $._expr_body)))),
      seq('if', $._not_paren, field('cond', $._expr), field('then', $._braced),
        'else', field('else', $._expr_body)),
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
        seq(':', field('body', $._expr_body)),
        field('body', $._body_block),
      ),
    )),
    match_default: $ => prec.right(seq(
      'default',
      choice(
        seq(':', field('body', $._expr_body)),
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
        optional(seq(':', field('index_type', choice($.base_type, $.identifier)))),
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
      field('then', $._expr_body))),

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
    apply_stam: $ => seq('apply', $._not_paren),

    // ========================= View declarations ==========================

    view_stam: $ => seq(
      field('kind', choice('view', 'edit')),
      optional(seq(field('name', $.identifier), ':')),
      field('pattern', $.view_pattern),
      optional(seq(parameter_list($), optional(field('type', $.return_type)))),
      optional(field('state', $.state_decl)),
      field('body', $._body_block),
      repeat(field('handler', $.event_handler)),
    ),

    state_decl: $ => seq('state', $.state_entry, repeat(seq(',', $.state_entry))),
    // `name: expr` is template-local state; a bare `name` binds engine-backed
    // state whose value the host owns, so the initializer is optional.
    state_entry: $ => seq(field('name', $.identifier),
      optional(seq(':', field('value', $._expr)))),

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
      'decimal', 'integer', 'number', 'none', 'datetime', 'date', 'time', 'binary',
      'range', 'list', 'array', 'map', 'element', 'object',
      'function', 'error', 'string', 'symbol',
      'i8', 'i16', 'i32', 'i64', 'u8', 'u16', 'u32', 'u64', 'f16', 'f32',
    ))),
    base_type: $ => choice($._base_type_kw, 'type'),

    occurrence: $ => choice($._uncounted_occurrence, $.occurrence_count),
    // Every suffix but the `{n}` count, which a declaration's return type never
    // takes. `+` and `*` are dual-role (S16.2.3), so as suffixes they carry the
    // same-line guard their binary readings carry: `type T = int` ⏎ `+ 1` is an
    // error, as in C, never `int+`. The guard is also what lets an `is` type
    // keep its suffix, since the scanner offers the guarded token first.
    _uncounted_occurrence: $ => choice('?', alias($._bin_plus, '+'),
      alias($._bin_star, '*'), $.array_count),
    // S11.1.6v2/S16.8.6v3: a count on a *run* is the occurrence family —
    // `T{n}` exactly, `T{n,m}` between, `T{n+}` at least, the open form
    // echoing the bare `+` rather than regex's trailing comma; the brackets
    // are the array family (`T[]`, `T[n]`). `T[n+]` and `T[n, m]` are gone.
    // Both openers take the same same-line guard as an index: `[` is dual-role
    // in TYPE space too, so `type T = int` ⏎ `[3]` must be the S16.2.3 error
    // rather than a silent continuation, and a line-start `{` is a new
    // statement, never a count. This is O3's rule applied on the grammar side:
    // type space shares the S16.2.2 continuation set instead of keeping its
    // own. The `{` guard is zero-width: C counts a brace only when it touches
    // the type and holds an integer (`int{2}`; `int {2}` is the type and a
    // block), and the scanner must see past the brace to tell.
    occurrence_count: $ => prec(2, choice(
      seq($._occurrence_lbrace, '{', $.integer, '}'),
      seq($._occurrence_lbrace, '{', $.integer, '+', '}'),
      seq($._occurrence_lbrace, '{', $.integer, ',', $.integer, '}'),
    )),
    array_count: $ => prec(2, choice(
      seq(alias($._index_lbracket, '['), ']'),
      seq(alias($._index_lbracket, '['), $.integer, ']'),
    )),

    // S11.1.1v3 / S11.1.6v2: a declaration's return type is a name with the
    // suffix chain any type takes (`int[][]`, `int?[]`, `int[]?`) except the
    // `{n}` count: after it a brace is always the body, spaced or tight, so a
    // counted return type goes through a type alias (the S11.1.6v2
    // conformance note; the C parser's LAMBDA_REDUCTION_FLAG_RETURN_TYPE).
    // Right-associative like `unary_type`: where the type ends an `is`
    // expression, a same-line `+` or `*` is the suffix, never arithmetic.
    return_occurrence_type: $ => prec.right(choice($._return_name,
      alias($._return_occurrence_type, $.occurrence_type),
      alias($._return_nullable_array_type, $.nullable_array_type),
      alias($._return_optional_array_type, $.optional_array_type))),
    _return_name: $ => choice($.base_type, $.identifier),
    ...suffix_chain(RETURN_CHAIN, $ => $._return_name,
      $ => alias($._uncounted_occurrence, $.occurrence)),
    return_type_pattern: return_alternatives($ => $.return_occurrence_type),
    return_type: return_contract($ => $.return_type_pattern),
    // A function TYPE has no body for the brace to open, so its return type
    // takes the count too, as C's type slot does (`fn (x: int) int{2}`). A
    // count never chains, so the counted name is one more alternative.
    _fn_return_type: return_contract(
      $ => alias($._fn_return_type_pattern, $.return_type_pattern)),
    // ... and, like C's type slot, a function type: `fn (x: int) fn (y: int) int`.
    _fn_return_type_pattern: return_alternatives($ => choice(
      $.return_occurrence_type,
      alias($._counted_return_type, $.return_occurrence_type),
      $.fn_type)),
    _counted_return_type: $ => alias(seq(
      field('operand', $._return_name),
      field('operator', alias($._count_occurrence, $.occurrence)),
    ), $.occurrence_type),
    _count_occurrence: $ => $.occurrence_count,

    list_type: $ => prec.dynamic(2, seq(
      '(', seq($._binder_capable_type,
        repeat(seq(',', $._binder_capable_type))), ')',
    )),
    array_type: $ => seq('[', comma_sep($._binder_capable_type), ']'),
    map_type_item: $ => seq(
      field('name', $._field_name),
      optional(field('optional', '?')),  // §7.22: optional FIELD, as in object types
      ':', field('as', $._binder_capable_type),
    ),
    map_type: $ => seq('{',
      optional(seq($.map_type_item, repeat(seq(',', $.map_type_item)))), '}',
    ),

    pattern_attr_type: $ => prec(1, seq(
      field('name', $._field_name),
      // §7.22: `a?: T` marks the FIELD optional (it may be absent);
      // `a: T?` makes the VALUE nullable. The two are different claims.
      optional(field('optional', '?')),
      ':', field('as', $._binder_capable_type),
      optional(seq('=', field('default', $._non_null_literal))),
    )),
    content_type: $ => seq($._binder_capable_type,
      repeat(seq(',', $._binder_capable_type))),

    // A namespace-qualified tag is legal in an element VALUE (S2.4.3v2), so an
    // element TYPE must admit one too — `type T = <soap.Fault …>`.
    element_type: $ => seq('<', choice($.dotted_name, $._data_name), choice(
      seq(alias($.pattern_attr_type, $.attr),
        repeat(seq(',', alias($.pattern_attr_type, $.attr))),
        optional(seq(',', $.content_type))),
      optional($.content_type),
    ), '>'),

    fn_param: $ => seq(
      field('name', $.identifier), seq(':', field('type', $._type_pattern)),
    ),
    // `fn` and `pn` function types are disjoint by colour; `function` is
    // their union (S11.1.5v2). Alone, `fn` and `pn` are the colours themselves
    // (`f is fn`); a signature always spells its parameter list and may add a
    // return type -- `fn ()`, `fn () int`, never `fn int`. The return type
    // starts on the `)` line: `_fn_return` is emitted only there, and a name
    // opening the next line is an error (S16.2.3v3).
    fn_type: $ => prec.right(seq(
      field('kind', choice('fn', 'pn')),
      optional(seq(
        '(', optional(seq(field('declare', $.fn_param),
          repeat(seq(',', field('declare', $.fn_param))))), ')',
        optional(seq($._fn_return,
          field('type', alias($._fn_return_type, $.return_type)))),
      )),
    )),

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
      $.char_pattern_island,
    ),

    // occurrence_type, nullable_array_type (`int[2][3]` is three arrays of
    // two; `int?[]` an array of nullable ints) and optional_array_type (the
    // nullable array `int[]?`, S11.1.6v2's `T | null`): see `suffix_chain`.
    ...suffix_chain(TYPE_CHAIN, $ => $.primary_type, $ => $.occurrence),
    // A chain head outranks the lone suffix it also spells: where an `is` type
    // ends an arrow body, the next `[` or `?` continues the type, as in C's
    // greedy type slot, rather than indexing or querying the arrow.
    _array_occurrence: $ => prec(1, $.array_count),
    _nullable_occurrence: _ => prec(1, '?'),
    negation_type: $ => prec.right(seq('!', field('operand', $.primary_type))),

    unary_type: $ => prec.right(choice(
      $.optional_array_type,
      $.nullable_array_type,
      $.occurrence_type,
      $.negation_type,
      $.primary_type,
    )),
    binary_type: $ => choice(...type_operators($, $._type_pattern)),

    _type_pattern: $ => choice($.unary_type, $.binary_type, $.fn_type),

    // A type binder is a parameter-boundary construct. Its base is a complete
    // annotation (including a `that` refinement), so `as T` binds looser than
    // type operators while declarations, schemas, and return contracts retain
    // their ordinary type grammar (S4.2.2, D3.3.3v3).
    _parameter_annotation_type: $ => choice($.leading_binder_type,
      $.binder_type, $._annotation_type),
    // A level-1 binder can occur below an array, map, element, or tuple in a
    // parameter annotation. The production parser still reserves that syntax
    // outside parameter annotations (S4.2.2, D3.3.3v3).
    _binder_capable_type: $ => choice($.binder_type, $._type_pattern),
    // Base-type spellings are parsed here only so the direct binder builder
    // can issue its dedicated collision diagnostic (S11.4.8).
    _binder_name: $ => choice($.identifier,
      alias($._base_type_kw, $.base_type), alias('type', $.base_type)),
    binder_type: $ => prec.right(seq(
      field('base', $._annotation_type), 'as', field('binder', $._binder_name),
    )),
    leading_binder_type: $ => prec.right(seq(
      'as', field('binder', $._binder_name),
    )),

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
        alias($._method_stam, $.fn_stam),
        alias($._method_expr_stam, $.fn_expr_stam),
        $._type_pattern,
      ))),
      '}',
    ),

    // ==================== String / symbol pattern islands =================

    _char_pattern_tag: _ => token(choice('\\symbol(', '\\(')),
    char_pattern_island: $ => seq(
      field('tag', $._char_pattern_tag), field('body', $._char_pattern_expr), ')',
    ),

    char_class: _ => choice('...', 'd', 'w', 's', 'a', '.'),
    _char_primary_type: $ => choice(
      $.range_type, $._non_null_literal, $.identifier, $.char_class,
    ),
    char_occurrence_type: $ => prec.right(seq(
      field('operand', $._char_primary_type), field('operator', $.occurrence),
    )),
    char_negation_type: $ => prec.right(seq(
      '!', field('operand', $._char_primary_type),
    )),
    char_grouped_type: $ => prec.right(seq(
      optional('!'), '(', $._char_pattern_expr, ')',
      optional(field('occurrence', $.occurrence)),
    )),
    char_concat_type: $ => prec.left(1, seq(
      choice($.char_unary_type, $.char_grouped_type),
      repeat1(choice($.char_unary_type, $.char_grouped_type)),
    )),
    char_binary_type: $ => choice(...type_operators($, $._char_pattern_expr)),
    char_unary_type: $ => prec.right(choice(
      $.char_occurrence_type,
      $.char_negation_type,
      $._char_primary_type,
    )),
    _char_pattern_expr: $ => choice(
      $.char_unary_type,
      $.char_concat_type,
      alias($.char_binary_type, $.binary_type),
      $.char_grouped_type,
    ),

    // ============================== Imports ===============================

    // S16.9.6: `.` is the only import separator (`import .a.b`). A segment
    // names a module file, not a binding, so a keyword spells one, as C's
    // `token_is_key` reads it.
    _module_segment: $ => choice($.identifier, alias($._keyword_name, $.identifier)),
    relative_name: $ => repeat1(seq('.', $._module_segment)),
    absolute_name: $ => seq($._module_segment, repeat(seq('.', $._module_segment))),
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

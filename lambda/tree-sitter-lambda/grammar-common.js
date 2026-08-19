/**
 * @file Lambda Script grammar for tree-sitter
 * @author Henry Luo
 * @license MIT
 */

// @ts-check
/// <reference types="../tree-sitter-dsl.d.ts" />

// rule for one or more of the rules separated by a comma
function comma_sep1(rule) {
  return seq(rule, repeat(seq(',', rule)));
}

function comma_sep(rule) {
  return optional(comma_sep1(rule));
}

function qualified_name($, precedence) {
  return prec.left(precedence, seq(
    choice($.identifier, $.symbol),
    repeat1(seq('.', choice($.identifier, $.symbol))),
  ));
}

const linebreak = /\r\n|\n/;
const decimal_digits = /\d+/;
const integer_literal = seq(choice('0', seq(/[1-9]/, optional(decimal_digits))));
const hex_integer_literal = seq('0', choice('x', 'X'), /[0-9a-fA-F]+/);
const exponent_part = seq(choice('e', 'E'), optional(choice('+', '-')), decimal_digits);
// C16 ruling 9 (revised): an unsuffixed literal's type is LEXICAL, and an
// EXPONENT makes it a float -- `1e2` is float 100.0, as in C, Python, Java,
// Go, Rust, Swift, Ruby and Lua. An earlier revision split the exponent by
// sign so `10e1` lexed as int; that made `1e16` and `1e100` fail to parse (they
// fall outside int's band) while the identical `1.0e16` parsed fine, a
// distinction no other language draws. Nothing is lost by conceding the
// convention: C16 makes `int` the float64-representable integers, a SUBSET of
// float admitted by membership, so `let n: int = 1e2` still holds.
const float_literal = choice(
  seq(integer_literal, '.', decimal_digits, optional(exponent_part)),
  seq('.', decimal_digits, optional(exponent_part)),
  seq(integer_literal, exponent_part),
);
const decimal_literal = choice(
  seq(integer_literal, '.', decimal_digits),
  seq('.', decimal_digits),
);

// sized integer suffixes: i8, i16, i32, i64, u8, u16, u32, u64
const sized_int_suffix = choice('i8', 'i16', 'i32', 'i64', 'u8', 'u16', 'u32', 'u64');
// sized float suffixes: f16, f32, f64
const sized_float_suffix = choice('f16', 'f32', 'f64');

// need to exclude relational exprs in attr (to avoid conflicts with element tags)
// pipe/filter operators are always included
function binary_expr($, in_attr) {
  let operand = in_attr ? choice($.primary_expr, $.unary_expr, alias($.attr_binary_expr, $.binary_expr))
                        : $._expr;
  let ops = [
    ['+', 'binary_plus'],
    ['++', 'binary_plus'],
    ['-', 'binary_plus'],
    ['*', 'binary_times'],
    ['/', 'binary_times'],
    ['div', 'binary_times'],
    ['%', 'binary_times'],
    ['**', 'binary_pow', 'right'],
    [$._binary_eq_symbol_op, 'binary_eq'],
    [$._binary_eq_word_op, 'binary_eq'],
    [$._binary_word_relation_op, 'binary_relation'],
    // Relational operators - excluded in attr to avoid element tag conflicts
    ...(in_attr ? [] :
      [['<', 'binary_relation'],
      ['<=', 'binary_relation'],
      ['>=', 'binary_relation'],
      ['>', 'binary_relation']]),
    ['and', 'logical_and'],
    ['or', 'logical_or'],
    ['to', 'range_to'],
    ['|', 'set_union'],
    // Pipe/filter operators - always included (even in attr context)
    ['|>', 'pipe'],
    ['that', 'pipe'],  // filter operator: items that ~ > 0
    ['&', 'set_intersect'],
    ['!', 'set_exclude'],  // set1 ! set2, elements in set1 but not in set2.
    ['is', 'is_in'],
    ['in', 'is_in'],
    [$._at, 'is_in'],
  ];
  return ops.map(([operator, precedence, associativity]) =>
    (associativity === 'right' ? prec.right : prec.left)(precedence, seq(
      field('left', operand),
      field('operator', operator),
      field('right', operand),
    )),
  );
}

function type_pattern(type_expr) {
  return [
    ['|', 'set_union'],
    ['&', 'set_intersect'],
    ['!', 'set_exclude'],  // set1 ! set2, elements in set1 but not in set2.
    // ['^', 'set_exclude'],  // set1 ^ set2, elements in either set, but not both.
  ].map(([operator, precedence, associativity]) =>
    (associativity === 'right' ? prec.right : prec.left)(precedence, seq(
      field('left', type_expr),
      field('operator', operator),
      field('right', type_expr),
    )),
  );
}

function _attr_content_type($) {
  return choice(
    seq(alias($.pattern_attr_type, $.attr), repeat(seq(',', alias($.pattern_attr_type, $.attr))),
      optional(seq(choice(linebreak, ';'), $.content_type))
    ),
    optional($.content_type)
  );
}

// ---------------------------------------------------------------------------
// Shared core. Consumed by BOTH grammar-lambda.js (the official full grammar)
// and grammar.js (the production grammar, whose type layer is external scanner
// tokens). Every rule outside the type-pattern sub-language lives here, so the
// two grammars differ only in that layer.
//
// Seam: the core references four names each type layer must define —
//   _type_pattern   the annotation-position type pattern
//   _primary_type   a single primary type (query_expr operand, view atoms)
//   pattern_island  a string/symbol pattern island in value position
//   content_type    element/object content schema
// ---------------------------------------------------------------------------

module.exports = {
  linebreak, comma_sep, comma_sep1, qualified_name, binary_expr, type_pattern,
  _attr_content_type,

  options: {
    extras: $ => [
      /\s/,
      $.comment,
    ],

    externals: $ => [
      $._start,
    ],

    word: $ => $.identifier,

    // an array of hidden rule names for the generated node types
    // supertype symbols must always have a single visible child
    supertypes: $ => [
      // $._expr,
    ],

    inline: $ => [
      // seam rules are pure aliases; splicing them keeps the parse tables the
      // size they were before the grammar was split (they cost ~850 large
      // states and 240KB of parser.o otherwise)
      $._primary_type,
      $._view_atom_type,
      $._value_island,
      $._non_null_literal,
      $._parenthesized_expr,
      $._arguments,
      $._number,
      $._key
    ],

    conflicts: $ => [
      // The postfix-chain conflicts that used to sit here became unnecessary
      // once the type layer stopped reaching into the expression grammar; the
      // generator reports them as such and they cost nothing either way.
      [$._expr, $.query_expr],                       // expr ? or .? could end expr or start query
      [$.dotted_name, $.path_expr],                  // dotted attributes vs rooted path steps
    ],

    precedences: $ => [
    // value expr precedences
    [
      $.fn_expr_stam,
      'propagate',
      $.call_expr,
      $.index_expr,
      'member',
      $.nav_expr,
      $.primary_expr,
      $.unary_expr,
      // statement end: linebreak terminates statement before binary operators can continue
      'statement_end',
      // binary operators
      'binary_pow',
      'binary_times',
      'binary_plus',
      'binary_relation',
      'binary_eq',
      // set operators
      'range_to',
      'set_intersect',  // like *
      'set_exclude',    // like -
      'set_union',      // like or
      // logic operators
      'is_in',
      'logical_and',
      'logical_or',    
      // pipe operators (low precedence, just above control flow)
      'pipe',
      $.if_expr,
      $.match_expr,
      $.for_expr,
      $.let_expr,
      $.assign_expr,
      $.assign_stam,
    ],
    [$.attr_binary_expr, $._attr_expr]
  ]
  },


  // Precedences naming type-layer rules. Only the full grammar has those
  // rules; the production grammar's type layer is a scanner token, which
  // needs no precedence relation.
  typePrecedences: $ => [
    [
      $.range_type,
      $.primary_type,
      $.unary_type,         // tight unary types 
      $.binary_type,        // alternation (|, &, !)
      $.negation_type,      // A ! B has higher precedence than A (!B)
      $._type_pattern,   
      $.return_type,   
      $.fn_type,            // fn binds loosest: fn int+ means fn (int+)
    ],
  ],

  coreRules: {
    document: $ => optional(choice(
      seq(
        prec.left(seq(
          $._import_stam, repeat(seq(choice(linebreak, ';'), $._import_stam)),
        )),
        optional(seq( choice(linebreak, ';'), $.content )),
      ),
      $.content
    )),
    comment: _ => token(prec(1, choice(
      seq('//', /[^\r\n\u2028\u2029]*/),
      seq(
        '/*',
        /[^*]*\*+([^/*][^*]*\*+)*/,
        '/',
      ),
    ))),

    // Literal Values

    // String as single token to prevent /* inside strings being parsed as comments
    // Matches: "", "content", "content with \" escapes"
    // Escape sequences: \", \\, \/, \b, \f, \n, \r, \t, \uXXXX, \u{X...}
    string: _ => token(seq(
      '"',
      repeat(choice(
        /[^"\\]+/,  // any chars except " and \
        /\\["\\\/bfnrt]/,  // simple escapes
        /\\u[0-9a-fA-F]{4}/,  // \uXXXX
        /\\u\{[0-9a-fA-F]+\}/,  // \u{X...}
      )),
      '"',
    )),

    // Symbol as single token (same reason as string)
    // Symbols don't allow newlines within them
    symbol: _ => token(seq(
      "'",
      repeat1(choice(
        /[^'\\\n]+/,  // any chars except ', \, and newline
        /\\['\\\/bfnrt]/,  // simple escapes
        /\\u[0-9a-fA-F]{4}/,  // \uXXXX
        /\\u\{[0-9a-fA-F]+\}/,  // \u{X...}
      )),
      "'",
    )),

    // binary token: b'...' containing hex or base64 data
    // Actual parsing done by AST builder
    binary: _ => token(seq("b'", repeat1(/[^']/), "'")),
    _number: $ => choice($.imaginary, $.integer, $.float, $.decimal, $.sized_integer, $.sized_float),

    // Keep the imaginary suffix in one token so `4j` cannot be parsed as an
    // integer followed by an identifier.  Signs remain unary operators.
    imaginary: _ => token(seq(choice(float_literal, integer_literal, 'inf', 'nan'), 'j')),
    integer: _ => token(choice(hex_integer_literal, integer_literal)),
    float: _ => token(float_literal),

    // 'n' = integer, 'm' = decimal (A.5 suffix split). The grammar accepts
    // both suffixes on every numeric spelling so that fractional 'n'
    // (e.g. 1.5n) reaches the AST builder and gets a targeted error
    // pointing at 'm', instead of an opaque parse error.
    decimal: $ => token(seq(
      choice(float_literal, decimal_literal, integer_literal),
      choice('n', 'm')
    )),

    // sized integer: integer literal with type suffix (i8, i16, i32, i64, u8, u16, u32, u64)
    sized_integer: _ => token(seq(
      choice(hex_integer_literal, integer_literal),
      sized_int_suffix
    )),

    // sized float: float literal with type suffix (f16, f32, f64)
    sized_float: _ => token(seq(
      choice(
        seq(choice('0', seq(/[1-9]/, optional(/\d+/))), '.', /\d+/),
        seq('.', /\d+/)
      ),
      sized_float_suffix
    )),

    // datetime token: t'...' containing date/time text
    // Actual parsing done by AST builder via datetime_parse()
    datetime: _ => token(seq( "t'", repeat(choice(/[0-9]/, /[:\-+.tTzZ ]/)), "'" )),

    // Note: 'null' is now part of $.base_type, no separate rule needed
    // named_value combines scalar poison spellings into one token. Decimal poison
    // remains visibly decimal and round-trips through the canonical source form.
    named_value: _ => token(choice(
      'decimal.inf', 'decimal.nan', 'true', 'false', 'inf', 'nan'
    )),

    // Containers: list, array, map, element

    // expr statements that need ';'
    _expr_stam: $ => choice(
      $.let_stam,
      $.fn_expr_stam,
      $.type_stam,
    ),
    _content_expr: $ => choice(
      repeat1(choice($.string, $.map, $.element)),
      $.handler_expr,
      $._attr_expr,
      $._expr_stam
    ),

    // statement content
    _statement: $ => choice(
      $.object_type,
      $.if_stam,
      $.match_expr,
      $.for_stam,
      $.while_stam,
      $.fn_stam,
      $.view_stam,
      $.break_stam,
      $.continue_stam,
      $.return_stam,
      $.raise_stam,
      $.var_stam,
      $.assign_stam,
      $.apply_stam,
      prec.right('statement_end', seq($._content_expr, choice(token(prec(10, /\r\n|\n/)), ';'))),
    ),
    content: $ => choice(
      seq(
        repeat1($._statement),
        optional($._content_expr)
      ),
      $._content_expr
    ),

    // list rule removed — (expr) is now only grouping via _parenthesized_expr
    // [a, b, c] is the only ordered sequence syntax

    // Literals and Containers
    _non_null_literal: $ => choice(
      $._number,
      $.string,
      $.symbol,
      $.datetime,
      $.binary,
      $.named_value,
    ),
    _key: $ => choice($.symbol, $.identifier, $.base_type, $.last_index, '*'),
    map_item: $ => seq( field('name', $._key), ':', field('as', $._expr) ),
    map: $ => seq( '{', comma_sep($.map_item), '}' ),
    array: $ => seq( '[', comma_sep($._expr), ']'),
    range: $ => seq( $._expr, 'to', $._expr ),
    attr_binary_expr: $ => choice(
      ...binary_expr($, true),
    ),

    // expr excluding comparison exprs (for element attributes where < > conflict with tags)
    _attr_expr: $ => choice(
      $.primary_expr,
      $.unary_expr,
      alias($.attr_binary_expr, $.binary_expr),
      $.if_expr,
      $.for_expr,
    ),

    // Attribute names may be qualified keys such as svg.width.
    attr_name: $ => choice(alias($._attr_dotted_name, $.dotted_name), $._key),
    _attr_dotted_name: $ => qualified_name($, 51),
    attr: $ => seq( field('name', $.attr_name), ':', field('as', $._attr_expr) ),

    // Dotted name: arbitrary depth dotted segments
    // Each segment is an identifier or symbol: a.b.'c'.d
    // Keep this below primary/member expressions in value positions; it is a
    // qualified name for element and attribute positions, not a primary expr.
    dotted_name: $ => qualified_name($, 49),
    element: $ => seq('<',
      choice($.dotted_name, $.symbol, $.identifier),
      optional(
        seq(
          $.attr,
          repeat(seq(',', $.attr)),
        ),
      ),
      optional(
        seq(optional(choice(linebreak, ';')), $.content)
      ),
      '>'
    ),

    // Expressions
    _parenthesized_expr: $ => seq(
      '(',
      choice(
        $._expr,
        // Prefer this prefix over reducing its first `let` as a complete expr.
        seq(repeat1(prec(1, seq($.let_expr, ','))), $._expr),
      ),
      ')',
    ),
    _expr: $ => choice(
      $.primary_expr,
      $.unary_expr,
      $.binary_expr,
      $.let_expr,
      $.if_expr,
      $.match_expr,
      $.for_expr,
      $.raise_expr,
    ),

    // raise expression - raises an error in functional context
    raise_expr: $ => prec.right(seq(
      'raise', field('value', $._expr)
    )),

    // prec(50) to make primary_expr higher priority than content
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
      $.element,
      $.base_type,  // includes null
      $._value_island, // inline string/symbol type pattern
      $.identifier,
      $.index_expr,
      $.path_expr,   // / or . paths with optional segment
      $.member_expr,
      $.nav_expr,     // expr.~~ / expr./ navigation
      $.handler_expr,
      $.propagate_expr,
      $.call_expr,
      $.start_expr,
      $.query_expr,         // expr?T or expr.?T - query by type
      $._parenthesized_expr,
      $.fn_expr,    // arrow fn: (params) => expr - colocated with list for GLR
      $.current_expr,   // ~ or ~# for pipe context
      $.current_parent_expr, // ~~ is exactly ~.~~
      $.current_error_expr, // ^ inside an active error-handler body
      $.variadic,       // ... (to prevent ... being parsed as .. + .)
    )),
    _arguments: $ => seq(
      '(', comma_sep( field('argument', choice($.named_argument, $._expr)) ), ')',
    ),
    call_expr: $ => prec.right(100, seq(
      field('function', choice($.primary_expr, 'import')),
      $._arguments,
  )),

    // propagation owns its caret at the same postfix-primary tier as member
    // access; wider operands must be parenthesized before this rule applies.
    propagate_expr: $ => prec.left(100, seq(
      field('operand', $.primary_expr),
      field('propagate', '^'),
    )),

    // handlers own their caret at the same postfix-primary tier as member
    // access. The optional `~` arm receives the non-error operand value. The
    // complete result remains primary-like, so `.field`, indexing, calls,
    // propagation, and another handler continue through the normal chain.
    handler_expr: $ => prec.left(100, seq(
      field('operand', $.primary_expr),
      '^', '{', field('body', $.content), '}',
      optional(seq('~', '{', field('value', $.content), '}')),
    )),

    // `_start` is scanned contextually so ordinary identifiers named `start`
    // remain valid in parameters, fields, bindings, and call positions.
    start_expr: $ => prec.right(90, seq(
      $._start, field('operand', $.call_expr),
    )),

    // Indexing: arr[i] for 1-D / chained, or arr[i, j, k] for N-D multi-dim
    // (NumPy/Julia/R/C++23 style; comma-separated indices resolve to a single
    // stride-walking offset on N-D ArrayNum).
    index_expr: $ => prec.right(100, seq(
      field('object', $.primary_expr),
      '[',
      field('field', $._expr),
      repeat(seq(',', field('field', $._expr))),
      ']',
    )),
    last_index: _ => token(prec(2, 'last')),

    // Query expression: expr?T (recursive) or expr.?T (direct)
    query_expr: $ => seq(
      field('object', $.primary_expr),
      field('op', choice('?', '.?')),
      field('query', $._primary_type),
    ),

    // Path prefix: / or . for rooted/relative path expressions.
    _path_prefix: _ => token(choice('/', '.')),

    // Variadic marker: ... (higher priority than path_parent)
    variadic: _ => token(prec(2, '...')),
    var_param_marker: _ => token(prec(3, 'var')),

    // Navigation operations. `~~` is the sole parent step and `/` is the
    // postfix root step; both share member precedence.
    path_parent: _ => token(prec(3, '~~')),
    path_root: _ => token(prec(3, '/')),

    // Path expression: S2.4.1v2 admits rooted `/.a` and relative `.a` forms;
    // the retired bare `/`, bare `.`, and `/a` spellings are not path values.
    path_expr: $ => prec.right(choice(
      seq('/.', field('field', choice($.identifier, $.symbol,
        $.integer, $.path_wildcard, $.base_type, $.path_parent))),
      seq('.', field('field', choice($.identifier, $.symbol,
        $.integer, $.path_wildcard, $.base_type, $.path_parent, $.path_root)))
    )),

    // Member access is the value form of dot syntax; dotted_name is reserved
    // for qualified element and attribute names.
    // member access must bind before a call so qualified calls stay intact
    // when their argument list is parsed inside a procedural block.
    member_expr: $ => prec.left(110, seq(
      field('object', choice($.primary_expr, $.member_expr, $.nav_expr)), '.',
      field('field', choice($.identifier, $.symbol, $.integer, $.path_wildcard, $.base_type))
    )),

    // Root/parent access after an expression. Keep the operation as a named
    // child so the AST can distinguish it from an ordinary member named
    // `parent`.
    nav_expr: $ => prec.left('member', seq(
      field('object', choice($.primary_expr, $.member_expr, $.nav_expr)),
      '.',
      field('operation', choice($.path_parent, $.path_root))
    )),

    // Bare `~~` is the contextual parent atom and lowers to `~.~~`.
    current_parent_expr: _ => token(prec(4, '~~')),

    // Path wildcard: * (single segment) or ** (recursive, zero or more segments)
    path_wildcard: _ => token(choice('**', '*')),

    _binary_eq_symbol_op: _ => token(choice('==', '!=')),
    _binary_eq_word_op: _ => token(choice('eq', 'ne')),
    _binary_word_relation_op: _ => token(choice('lt', 'le', 'ge', 'gt')),
    binary_expr: $ => choice(
      ...binary_expr($, false),
    ),

    // Current item (~) or key/index (~#) reference in pipe context
    current_expr: _ => token(choice('~#', '~')),

    // current error reference. The token is admitted by the general expression
    // grammar so member/index builders can compose with it; build_ast enforces
    // that it occurs only in a handler body.
    current_error_expr: _ => prec(0, token('^')),
    _at: _ => token(prec(2, 'at')),
    _into: _ => token(prec(2, 'into')),

    // Unary expression. Error tests use the ordinary `is error` binary form;
    // caret is reserved for postfix propagation, handlers, and handler-local
    // current-error access.
    unary_expr: $ => choice(
      prec.left(90, seq(
        field('operator', choice('not', '!', '-', '+', '*')),
        field('operand', $._expr),
      )),
    ),
    identifier: _ => {
      // ECMAScript 2023-compliant identifier regex:
      // const identifierRegex = /^[$_\p{ID_Start}][$_\u200C\u200D\p{ID_Continue}]*$/u;

      // 'alpha' and 'alphanumeric' here, copied from Tree-sitter JS grammar,
      // are not exactly the same as ECMA standard, which is a limitation of Tree-sitter RegEx
      const alpha = /[^\x00-\x1F\s\p{Zs}0-9:;`"'@#.,|^&<=>+\-*/\\%?!~()\[\]{}\uFEFF\u2060\u200B\u2028\u2029]|\\u[0-9a-fA-F]{4}|\\u\{[0-9a-fA-F]+\}/;
      const alphanumeric = /[^\x00-\x1F\s\p{Zs}:;`"'@#.,|^&<=>+\-*/\\%?!~()\[\]{}\uFEFF\u2060\u200B\u2028\u2029]|\\u[0-9a-fA-F]{4}|\\u\{[0-9a-fA-F]+\}/;
      return token(seq(alpha, repeat(alphanumeric)));
    },

    // Function parameter: identifier/symbol with optional var, type, and default.
    // Supports: name, name?, name: type, name?: type, name = default, name: type = default
    parameter: $ => choice(
      seq(
        optional(field('var', $.var_param_marker)),
        field('name', choice($.identifier, $.symbol)),
        optional(field('optional', '?')),  // optional marker BEFORE type
        optional(seq(':', field('type', $._annotation_type))),
        optional(seq('=', field('default', $._expr))),
      ),
      field('variadic', $.variadic),  // variadic marker (must be last parameter)
    ),

    // Named argument in function call: name: value
    named_argument: $ => seq(
      field('name', choice($.identifier, $.symbol)),
      ':',
      field('value', $._expr),
    ),

    // fn with stam body
    fn_stam: $ => seq(
      optional(field('pub', 'pub')), // note: pub fn is only allowed at global level
      field('kind', choice('fn','pn')), field('name', choice($.identifier, $.symbol)),
      '(', optional(field('declare', $.parameter)), repeat(seq(',', field('declare', $.parameter))), ')',
      // return type with optional error type: T or T^E or T^
      optional(field('type', $.return_type)),
      '{', field('body', $.content), '}',
    ),

    // view/edit template declaration
    // Syntax: view [name:] pattern [(params)] [return_type] [state k:v, ...] { body } [on event() { ... }]*
    // Pattern is required; () optional unless return type present; name: optional
    view_stam: $ => seq(
      field('kind', token(prec(1, choice('view', 'edit')))),
      // optional name: (colon disambiguates name from pattern)
      optional(seq(field('name', $.identifier), ':')),
      // model pattern — element, map, type name, or union with |
      // Uses view_pattern (restricted _type_expr) to avoid {map_type}/{body} ambiguity
      field('pattern', $.view_pattern),
      // optional params and return type — () required if return type present
      optional(seq(
        '(', optional(seq(field('declare', $.parameter), repeat(seq(',', field('declare', $.parameter))))), ')',
        optional(field('type', $.return_type)),
      )),
      // optional state declarations
      optional(field('state', $.state_decl)),
      // body — functional (fn) semantics
      '{', field('body', $.content), '}',
      // zero or more event handlers
      repeat(field('handler', $.event_handler)),
    ),

    // View pattern: element or type name (identifier / base_type).
    // map_type is intentionally excluded to avoid ambiguity with the body {}.
    // Atom: element_type | identifier | base_type (with optional occurrence)
    // Union: atom | atom | ...
    _view_pattern_atom: $ => $._view_atom_type,
    view_pattern: $ => choice(
      $._view_pattern_atom,
      alias($.view_pattern_union, $.binary_type),
    ),
    view_pattern_union: $ => prec.left('set_union', seq(
      field('left', $._view_pattern_atom),
      field('operator', '|'),
      field('right', choice($._view_pattern_atom, alias($.view_pattern_union, $.binary_type))),
    )),

    // State declarations: state name: val, name: val, ...
    state_decl: $ => seq(
      'state',
      $.state_entry, repeat(seq(',', $.state_entry)),
    ),
    state_entry: $ => seq(
      field('name', $.identifier), ':', field('value', $._expr),
    ),

    // Event handler: on event_name(param) { body }
    // Handler body uses procedural (pn) semantics
    event_handler: $ => seq(
      'on', field('event', $.identifier),
      '(', optional(field('declare', $.parameter)), ')',
      '{', field('body', $.content), '}',
    ),

    // Note: apply; (bare apply statement) is a splat statement that
    // re-dispatches each child of the matched item (~) through the template
    // registry. Equivalent to `for (c in ~) apply(c)`. Only valid inside a
    // view/edit body; rejected elsewhere by build_ast semantic analysis.
    // Tokenized as a single lex unit (no whitespace allowed between 'apply'
    // and ';') so that `apply(arg)` keeps parsing as a regular call.
    apply_stam: $ => token(seq('apply', ';')),

    // fn with expr body; to KISS and we don't support pn expr
    fn_expr_stam: $ => seq(
      optional(field('pub', 'pub')), // note: pub fn is only allowed at global level
      'fn', field('name', choice($.identifier, $.symbol)),
      '(', optional(seq(field('declare', $.parameter), repeat(seq(',', field('declare', $.parameter))))), ')',
      // return type with optional error type: T or T^E or T^
      optional(field('type', $.return_type)),
      '=>', field('body', $._expr)
    ),

    // Anonymous Function (arrow expression)
    //
    // The untyped branch must remain an expression list. With `parameter` here,
    // Tree-sitter reduces `(x)` to a parenthesized expression before it sees
    // `=>`; the distinct branch preserves the shift needed for arrow heads.
    fn_expr: $ => prec.right(choice(
      prec.dynamic(1, seq(
        '(', field('declare', $.parameter), repeat(seq(',', field('declare', $.parameter))), ')',
        optional(field('type', $.return_type)), '=>', field('body', $._expr),
      )),
      seq(
        '(', $._expr, repeat(seq(',', $._expr)), ')',
        optional(field('type', $.return_type)), '=>', field('body', $._expr),
      ),
      seq('(', ')', optional(field('type', $.return_type)), '=>', field('body', $._expr)),
    )),

    // use prec.right so the expression is consumed greedily
    // Single assignment: let x = expr
    // Positional decomposition: let a, b = expr
    // Named decomposition: let a, b at expr
    assign_expr: $ => prec.right(choice(
      // single variable assignment
      seq(
        field('name', choice($.identifier, $.symbol)),
        optional(seq(':', field('type', $._annotation_type))), '=', field('as', $._expr),
      ),
      // multi-variable decomposition: let a, b = expr OR let a, b at expr
      seq(
        field('name', choice($.identifier, $.symbol)),
        repeat1(seq(',', field('name', choice($.identifier, $.symbol)))),
        field('decompose', choice('=', $._at)),
        field('as', $._expr),
      ),
    )),
    let_expr: $ => seq(
      'let', field('declare', $.assign_expr)
    ),
    let_stam: $ => seq(
      choice('let', 'pub'),
      field('declare', $.assign_expr), repeat(seq(',', field('declare', $.assign_expr)))
    ),

    // Expression-form if: if (cond) expr else expr
    // Condition always in parens. Else is REQUIRED (ternary-style).
    // Both then and else can be a block { content } (preferred over map via prec.dynamic).
    if_expr: $ => prec.right(seq(
      'if', '(', field('cond', $._expr), ')',
      choice(
        prec.dynamic(1, seq('{', field('then', $.content), '}')),
        field('then', $._expr),
      ),
      'else', choice(
        prec.dynamic(1, seq('{', field('else', $.content), '}')),
        field('else', $._expr),
      ),
    )),

    // Block-form if: if cond { stam } [else { stam } | else if_stam | else expr]
    // Condition without required parens. Block body. Else can be expr (NEW).
    if_stam: $ => prec.right(1, seq(
      'if', field('cond', $._expr),
      '{', optional(field('then', $.content)), '}',
      optional(seq('else', choice(
        prec.dynamic(1, seq('{', optional(field('else', $.content)), '}')),
        field('else', $.if_stam),
        field('else', $._expr),
      ))),
    )),

    // Match expression — unified form with required braces
    // match expr { case_arms }
    // Each arm can be expression form (case T: expr) or statement form (case T { stmts })
    match_expr: $ => seq(
      'match', field('scrutinee', $._expr),
      '{',
      repeat1(choice($.match_arm, $.match_default)),
      '}'
    ),
    match_arm: $ => prec.right(seq(
      'case', field('pattern', $._annotation_type),
      choice(
        seq(':', field('body', $._expr)),
        seq('{', field('body', $.content), '}')
      )
    )),
    match_default: $ => prec.right(seq(
      'default',
      choice(
        seq(':', field('body', $._expr)),
        seq('{', field('body', $.content), '}')
      )
    )),

    // Loop variable binding with optional index and type-annotated key
    // Single variable: for v in expr
    // Two variables: for k, v in expr (k = index for arrays, key for maps)
    // Type-filtered: for k:int, v in expr (indexed key only) / for k:symbol, v in expr (named key only)
    loop_expr: $ => choice(
      // for value in expr
      seq(
        field('name', $.identifier),
        optional(field('optional', '?')),
        field('op', choice('in', $._at)),
        field('as', $._expr),
        optional(seq('on', field('on', $._expr)))
      ),
      // for key, value in expr (with optional type annotation on key)
      seq(
        field('index', $.identifier),
        optional(seq(':', field('index_type', $.identifier))),
        ',', field('name', $.identifier),
        optional(field('optional', '?')),
        'in', field('as', $._expr),
        optional(seq('on', field('on', $._expr)))
      ),
    ),

    // let clause within for: let name = expr
    for_let_clause: $ => seq(
      'let', field('name', $.identifier), '=', field('value', $._expr)
    ),

    // where clause: where expr
    // Use prec.dynamic to prefer this over binary 'where' in for context
    for_where_clause: $ => prec.dynamic(10, seq(
      'where', field('cond', $._expr)
    )),

    // order by clause: order by expr [asc|desc] [, expr [asc|desc], ...]
    order_spec: $ => seq(
      field('expr', $._expr),
      optional(field('dir', choice('asc', 'desc')))
    ),
    for_order_clause: $ => seq(
      'order', 'by', field('spec', $.order_spec),
      repeat(seq(',', field('spec', $.order_spec)))
    ),

    // group key spec: expr [as alias]
    group_key_spec: $ => seq(
      field('key', $.primary_expr),
      optional(seq('as', field('alias', $.identifier)))
    ),

    // group by clause: group by expr [as alias] [, expr [as alias], ...] into name
    for_group_clause: $ => prec.dynamic(10, seq(
      'group', 'by',
      field('spec', $.group_key_spec),
      repeat(seq(',', field('spec', $.group_key_spec))),
      $._into, field('name', $.identifier)
    )),

    // limit clause: limit expr or limit last expr
    for_limit_clause: $ => seq(
      'limit',
      optional(field('last', $.last_index)),
      field('count', $._expr)
    ),

    // offset clause: offset expr
    for_offset_clause: $ => seq(
      'offset', field('count', $._expr)
    ),

    // shared for clauses: fixed order where → group → order → limit → offset (like SQL)
    for_clauses: $ => repeat1(choice(
      field('where', $.for_where_clause),
      field('group', $.for_group_clause),
      field('order', $.for_order_clause),
      field('limit', $.for_limit_clause),
      field('offset', $.for_offset_clause),
    )),

    // use prec.right so the body expression is consumed greedily
    for_expr: $ => prec.right(seq(
      'for', '(',
      field('declare', $.loop_expr),
      repeat(seq(',', field('declare', $.loop_expr))),
      // optional let clauses (comma-separated after declarations)
      repeat(seq(',', field('let', $.for_let_clause))),
      // optional clauses: where → group → order → limit → offset
      optional($.for_clauses),
      ')',
      choice(
      prec.dynamic(1, seq('{', field('then', $.content), '}')),
      field('then', $._expr),
      )
    )),
    for_stam: $ => seq(
      'for',
      field('declare', $.loop_expr),
      repeat(seq(',', field('declare', $.loop_expr))),
      // optional let clauses
      repeat(seq(',', field('let', $.for_let_clause))),
      // optional clauses: where → group → order → limit → offset
      optional($.for_clauses),
      '{', field('then', $.content), '}'
    ),

    // while statement (procedural only)
    while_stam: $ => seq(
      'while', '(', field('cond', $._expr), ')',
      '{', field('body', $.content), '}'
    ),

    // break statement (procedural only)
    break_stam: $ => seq('break', optional(';')),

    // continue statement (procedural only)
    continue_stam: $ => seq('continue', optional(';')),

    // return statement (procedural only)
    // use prec.right to prefer consuming expression when present
    return_stam: $ => prec.right(seq(
      'return',
      optional(field('value', $._expr)),
      optional(';')
    )),

    // raise statement (procedural only) - raises an error to caller
    // Reuses raise_expr to avoid GLR conflict between raise_expr and raise_stam
    raise_stam: $ => prec.right(seq(
      $.raise_expr,
      optional(';')
    )),

    // var statement for mutable variables (procedural only)
    var_stam: $ => seq(
      'var', field('declare', $.assign_expr), repeat(seq(',', field('declare', $.assign_expr))),
      optional(';')
    ),

    // assignment statement for mutable variables (procedural only)
    // use prec.right to prefer consuming expression when present
    // supports: x = val, arr[i] = val, obj.field = val
    assign_stam: $ => seq(
      field('target', choice($.identifier, $.index_expr, $.member_expr)), '=', field('value', $._expr),
      optional(';')
    ),

    // Type Definitions: ----------------------------------

    // Occurrence modifiers for types: ?, +, *, [], [n], [n, m], [n+]
    occurrence: $ => choice('?', '+', '*', $.occurrence_count),

    // Occurrence count: [] (any), [n] (exact), [n, m] (range), [n+] (unbounded)
    // Higher precedence than primary_type to prefer occurrence over array_type
    occurrence_count: $ => prec(2, choice(
      seq('[', ']'),                                 // any count: T[]
      seq('[', $.integer, ']'),                      // exactly n: T[5]
      seq('[', $.integer, ',', $.integer, ']'),      // n to m: T[2, 5]
      seq('[', $.integer, '+', ']'),                 // n or more: T[3+]
    )),

    // Keep type-only keywords in one token; `type` remains separate because it
    // starts declarations, while the builder distinguishes the other spellings.
    _base_type_kw: _ => token(prec(1, choice(
      'null', 'any', 'bool', 'int64', 'int', 'float', 'f64', 'complex', 'decimal', 'integer', 'number',
      'datetime', 'date', 'time', 'binary', 'range',
      'list', 'array', 'map', 'element', 'entity', 'object', 'function',
      'error', 'string', 'symbol',
      'i8', 'i16', 'i32', 'i64', 'u8', 'u16', 'u32', 'u64', 'f16', 'f32'
    ))),
    base_type: $ => prec(1, choice(
      $._base_type_kw,
      'type'
    )),

    // Statement-level object field: full annotation — a constrained type and an
    // expression default. Reachable only from object_type, which the main
    // grammar parses as a statement.
    attr_type: $ => prec(1, seq(
      field('name', choice($.symbol, $.identifier)),
      ':', field('as', $._annotation_type),
      optional(seq('=', field('default', $._attr_expr))),
    )),

    // Statement-level annotation: a type pattern, optionally constrained.
    // Kept as a choice rather than an optional inside constrained_type so an
    // unconstrained annotation produces no wrapper node and the tree shape for
    // plain `let x: int` is unchanged.
    _annotation_type: $ => choice(
      $._type_pattern,
      $.constrained_type,
    ),

    // Constrained type: the top-tier annotation form (CT1v2/CT2).
    // `that` may follow a declaration's ':', 'type =', or 'case' — never inside
    // a pattern, so the predicate needs no parentheses: nothing type-level can
    // follow it. The predicate runs to the slot delimiter and uses ~ for the
    // value being checked, matching the value-level `that` filter.
    constrained_type: $ => prec.right(seq(
      field('base', $._type_pattern),
      'that', field('constraint', $._expr),
    )),

    // Keep these field names aligned with occurrence_type: the AST builder
    // reuses the occurrence constructor for return `T?` contracts. Without
    // them it receives a null operator node and dereferences it while building
    // a valid nullable return annotation.
    return_occurrence_type: $ => seq(
      field('operand', choice($.base_type, $.identifier)),
      optional(field('operator', $.occurrence)),
    ),

    // Simple type pattern for return types
    // This restriction avoids ambiguity with map_type in fn () T { ... }
    return_type_pattern: $ => prec.left(seq(
      field('type', $.return_occurrence_type),
      repeat(seq(choice('|', '&', '!'), field('type', $.return_occurrence_type)))
    )),

    // Return type with optional error type: T or T^ or T^E
    // T^ means function may return any error (shorthand for T | error)
    // T^E means function returns T on success, E on error (E must be simple)
    // simplified return_type substantially reduced the parser size
    return_type: $ => prec.right(seq(
      field('ok', $.return_type_pattern),
      optional(seq(
        '^',
        optional(field('error', $.return_type_pattern))
      ))
    )),
    
    type_assign: $ => seq(field('name', choice($.identifier, $.symbol)), '=', field('as',
      $._annotation_type)),

    // type_stam handles type aliases and delimited string/symbol patterns.
    type_stam: $ => seq(
      optional(field('pub', 'pub')),
      'type',
      field('declare', alias($.type_assign, $.assign_expr)),
      repeat(seq(',', field('declare', alias($.type_assign, $.assign_expr))))
    ),

    // Object-level constraint: `that expr`, delimited by ',' / ';' / '}' like
    // its fn_stam neighbours (CT6). prec.right keeps the predicate greedy so a
    // nested value-level `a that b` filter stays inside it (CT2).
    that_constraint: $ => prec.right(seq('that', field('constraint', $._expr))),

    // Object/element type with optional inheritance, content schema, and methods
    // Object (no content): type Point { x: float, y: float }
    // Element (with content): type Article { title: string, string, element; fn render() => ... }
    // Without content → object type; with content → element type
    object_type: $ => seq(
      optional(field('pub', 'pub')),
      'type', field('name', choice($.identifier, $.symbol)),
      optional(seq(':', field('base', choice($.identifier, $.symbol)))),
      '{',
      // optional fields and content (attrs have name:type, content is bare type_expr)
      optional(choice(
        seq(
          alias($.attr_type, $.attr), repeat(seq(',', alias($.attr_type, $.attr))),
          optional(seq(',', $.content_type)),
        ),
        $.content_type,
      )),
      // optional ';' introduces methods section
      optional(seq(';',
        repeat(choice($.fn_stam, $.fn_expr_stam, $.that_constraint))
      )),
      '}'
    ),

    // top-level type definitions: type_stam | object_type

    // ==================== Module Imports ====================
    relative_name: $ => repeat1(seq(
      choice('.', '\\'), $.identifier
    )),
    absolute_name: $ => seq(
      $.identifier, repeat(seq(choice('.', '\\'), $.identifier))
    ),
    import_module: $ => choice(
        field('module', choice($.absolute_name, $.relative_name, $.symbol)),
        seq(field('alias', $.identifier), ':',
          field('module', choice($.absolute_name, $.relative_name, $.symbol)))
    ),
    _import_stam: $ => seq(
      'import', $.import_module, repeat(seq(',', $.import_module)),
    ),
  },
};

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

const digit = /\d/;
const linebreak = /\r\n|\n/;
const ws = /\s*/;
const decimal_digits = /\d+/;
const sign = optional(seq('-', ws));
const integer_literal = seq(choice('0', seq(/[1-9]/, optional(decimal_digits))));
const signed_integer_literal = seq(sign, integer_literal);
const signed_integer = seq(sign, decimal_digits);
const exponent_part = seq(choice('e', 'E'), signed_integer);
const float_literal = choice(
  seq(signed_integer_literal, '.', optional(decimal_digits), optional(exponent_part)),
  seq(sign, '.', decimal_digits, optional(exponent_part)),
  seq(signed_integer_literal, exponent_part),
  seq(sign, 'inf'),
  seq(sign, 'nan'),
);
const decimal_literal = choice(
  seq(signed_integer_literal, '.', optional(decimal_digits)),
  seq(sign, '.', decimal_digits),
);

const base64_unit = /[A-Za-z0-9+/]{4}/;
const base64_padding = choice(/[A-Za-z0-9+/]{2}==/, /[A-Za-z0-9+/]{3}=/);

// need to exclude relational exprs in attr
function binary_expr($, in_attr) {
  let operand = in_attr ? choice($.primary_expr, $.unary_expr, alias($.attr_binary_expr, $.binary_expr)) : $._expression;
  return [
    ['+', 'binary_plus'],
    ['++', 'binary_plus'],
    ['-', 'binary_plus'],
    ['*', 'binary_times'],
    ['/', 'binary_times'],
    ['div', 'binary_times'],
    ['%', 'binary_times'],
    ['^', 'binary_pow', 'right'],
    ['==', 'binary_eq'],
    ['!=', 'binary_eq'],
    ...(in_attr ?[]:
      [['<', 'binary_relation'],
      ['<=', 'binary_relation'],
      ['>=', 'binary_relation'],
      ['>', 'binary_relation']]),
    ['and', 'logical_and'],
    ['or', 'logical_or'],
    ['to', 'range_to'],
    // Pipe operators - same precedence, left-to-right chaining
    ['|', 'pipe'],
    ['where', 'pipe'],  // same precedence as | for left-to-right chaining
    ['&', 'set_intersect'],
    ['!', 'set_exclude'],  // set1 ! set2, elements in set1 but not in set2.
    ['is', 'is_in'],
    ['in', 'is_in'],
  ].map(([operator, precedence, associativity]) =>
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

function time() {
  // time: hh:mm:ss.sss or hh:mm:ss or hh:mm or hh.hhh or hh:mm.mmm or hh
  return seq(digit, digit, optional(seq(':', digit, digit)), optional(seq(':', digit, digit)),
    optional((seq('.', digit, digit, digit))),
    // timezone
    optional(choice('z', 'Z', seq(choice('+', '-'), digit, digit, optional(seq(':', digit, digit)))))
  );
}

function built_in_types(include_null) {
  let types = [
    'any',
    'error',
    'bool',
    // 'int8', 'int16', 'int32', 'int64',
    'int',    // int32
    'int64',  // int64
    // 'float32', 'float64',
    'float',  // float64
    'decimal',  // big decimal
    'number',
    'date',
    'time',
    'datetime',
    'symbol',
    'string',
    'binary',
    'list',
    'array',
    'map',
    'element',
    'entity',
    'object',
    'type',
    'function',
  ];
  return include_null ? choice('null', ...types) : choice(...types);
}

function _attr_content_type($) {
  return choice(
    seq(alias($.attr_type, $.attr), repeat(seq(',', alias($.attr_type, $.attr))),
      optional(seq(choice(linebreak, ';'), $.content_type))
    ),
    optional($.content_type)
  );
}

module.exports = grammar({
  name: "lambda",

  extras: $ => [
    /\s/,
    $.comment,
  ],

  word: $ => $.identifier,

  // an array of hidden rule names for the generated node types
  // supertype symbols must always have a single visible child
  supertypes: $ => [
    // $._expression,
  ],

  inline: $ => [
    $._non_null_literal,
    $._parenthesized_expr,
    $._arguments,
    $._expr_stam,
    $._number,
    $._datetime,
    $._expression,
    $._attr_expr,
    $._import_stam,
    $._type_expr,
    $._statement,
    $._content_expr,
  ],

  precedences: $ => [[
    $.fn_expr_stam,
    $.primary_expr,
    $.unary_expr,
    // binary operators
    'binary_pow',
    'binary_times',
    'binary_plus',
    'binary_compare',
    'binary_relation',
    'binary_eq',
    'logical_and',
    'logical_or',
    // set operators
    'range_to',
    'set_intersect',  // like *
    'set_exclude',    // like -
    'set_union',      // like or
    'is_in',
    // pipe operators (low precedence, just above control flow)
    'pipe',
    $.if_expr,
    $.for_expr,
    $.let_expr,
    $.fn_expr,
    $.assign_expr
  ],
  [
    $.fn_type,
    $.primary_type,
    $.type_occurrence,
    $.binary_type,
  ],
  // Pattern precedences
  [
    $.primary_pattern,
    $.pattern_occurrence,
    $.pattern_negation,
    'pattern_concat',
    'pattern_range',
    'pattern_intersect',
    'pattern_union',
  ]],

  conflicts: $ => [
  ],

  rules: {
    document: $ => optional(choice(
      seq(
        prec.left(seq(
          $._import_stam, repeat(seq(choice(linebreak, ';'), $._import_stam)),
        )),
        optional(seq( choice(linebreak, ';'), $.content )),
      ),
      $.content
    )),

    comment: _ => token(choice(
      seq('//', /[^\r\n\u2028\u2029]*/),
      seq(
        '/*',
        /[^*]*\*+([^/*][^*]*\*+)*/,
        '/',
      ),
    )),

    // Literal Values

    // Empty strings ("") are allowed - they map to null at the data level
    string: $ => seq('"', optional($._string_content), '"'),

    _string_content: $ => repeat1(choice(
      $.string_content,
      $.escape_sequence,
    )),

    // String content: can span multiple lines
    string_content: _ => token.immediate(/[^\\"]+/),

    // Empty symbols ('') are allowed - they map to null at the data level
    symbol: $ => seq("'", optional($._symbol_content), "'"),

    _symbol_content: $ => repeat1(choice(
      $.symbol_content,
      $.escape_sequence,
    )),

    symbol_content: _ => token.immediate(/[^\\'\n]+/),

    escape_sequence: _ => token.immediate(seq(
      '\\',
      /(\"|\\|\/|b|f|n|r|t|u[0-9a-fA-F]{4}|u\{[0-9a-fA-F]+\})/,
    )),

    binary: $ => seq("b'", /\s*/, choice($.hex_binary, $.base64_binary), /\s*/, "'"),

    // whitespace allowed in hex and base64 binary
    hex_binary: _ => token(seq(optional("\\x"), repeat1(/[0-9a-fA-F\s]/))),

    base64_binary: _ => token(seq("\\64",
      repeat1(choice(base64_unit, /\s+/)), optional(base64_padding)
    )),

    _number: $ => choice($.integer, $.float, $.decimal),

    integer: _ => token(signed_integer_literal),

    float: _ => token(float_literal),

    decimal: $ => {
      // no e-notation for decimal, following JS bigint
      return token( seq(choice(decimal_literal, signed_integer_literal), choice('n','N')) );
    },

    // time: hh:mm:ss.sss or hh:mm:ss or hh:mm or hh.hhh or hh:mm.mmm
    time: _ => token.immediate(time()),
    // date-time
    datetime: _ => token.immediate(
      seq(optional('-'), digit, digit, digit, digit, optional(seq('-', digit, digit)), optional(seq('-', digit, digit)),
        optional(seq(/\s+|T|t/, time()))
      )),

    _datetime: $ => seq("t'", /\s*/, choice($.datetime, $.time), /\s*/, "'"),

    index: $ => {
      return token(integer_literal);
    },

    null: _ => 'null',
    true: _ => 'true',
    false: _ => 'false',
    inf: _ => 'inf',
    nan: _ => 'nan',

    // Containers: list, array, map, element

    _content_literal: $ => choice(
      // $._number, // not allowed, as 123 - 456 is expr
      // $.symbol, // not allowed, as it is ambiguous with ident expr
      $.string,
      $._datetime,
      $.binary,
      $.true,
      $.false,
      $.null
    ),

    // expr statements that need ';'
    _expr_stam: $ => choice(
      $.let_stam,
      $.pub_stam,
      $.fn_expr_stam,
      $.type_stam,
      $.string_pattern,
      $.symbol_pattern,
    ),

    _content_expr: $ => choice(
      repeat1(choice($.string, $.map, $.element)),
      $._attr_expr,
      $._expr_stam
    ),

    // statement content
    _statement: $ => choice(
      $.object_type,
      $.entity_type,
      $.if_stam,
      $.for_stam,
      $.while_stam,
      $.fn_stam,
      $.break_stam,
      $.continue_stam,
      $.return_stam,
      $.var_stam,
      $.assign_stam,
      seq($._content_expr, choice(linebreak, ';')),
    ),

    content: $ => choice(
      seq(
        repeat1($._statement),
        optional($._content_expr)
      ),
      $._content_expr
    ),

    list: $ => seq(
      '(', $._expression, repeat(seq(',', $._expression)), ')'
    ),

    // Literals and Containers
    _non_null_literal: $ => choice(
      $._number,
      $.string,
      $.symbol,
      $._datetime,
      $.binary,
      $.true,
      $.false,
    ),

    map_item: $ => seq(
      field('name', choice($.string, $.symbol, $.identifier)),
      ':',
      field('as', $._expression),
    ),

    map: $ => seq(
      // $._expression for dynamic map item
      '{', comma_sep(choice($.map_item, $._expression)), '}',
    ),

    array: $ => seq(
      '[', comma_sep($._expression), ']',
    ),

    range: $ => seq(
      $._expression, 'to', $._expression,
    ),

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
      $.fn_expr,
    ),

    attr: $ => seq(
      field('name', choice($.string, $.symbol, $.identifier)), // string accepted for JSON compatibility
      ':', field('as', $._attr_expr)
    ),

    element: $ => seq('<',
      choice($.symbol, $.identifier), // string not accepted for element name
      choice(
        seq(choice($.attr, $.map),
          repeat(seq(',', choice($.attr, $.map))),
          optional(
            seq(choice(linebreak, ';'), $.content),
          )
        ),
        optional( seq(optional(choice(linebreak, ';')), $.content) )
      ),'>'
    ),

    // Expressions

    _parenthesized_expr: $ => seq(
      '(', $._expression, ')',
    ),

    _expression: $ => choice(
      $.primary_expr,
      $.unary_expr,
      $.binary_expr,
      $.let_expr,
      $.if_expr,
      $.for_expr,
      $.fn_expr,
    ),

    // prec(50) to make primary_expr higher priority than content
    primary_expr: $ => prec(50, choice(
      $.null,
      $.true,
      $.false,
      $.inf,
      $.nan,
      $._number,
      $._datetime,
      $.string,
      $.symbol,
      $.binary,
      $.list,
      $.array,
      $.map,
      $.element,
      alias($._non_null_base_type, $.base_type),
      $.identifier,
      $.index_expr,  // like Go
      $.member_expr,
      $.call_expr,
      $._parenthesized_expr,
      $.current_item,   // ~ for pipe context
      $.current_index,  // ~# for pipe key/index
    )),

    spread_argument: $ => seq('...', $._expression),

    _arguments: $ => seq(
      '(', comma_sep(optional(
      field('argument', choice($.named_argument, $._expression, $.spread_argument)))), ')',
    ),

    import: _ => token('import'),

    call_expr: $ => seq(
      field('function', choice($.primary_expr, $.import)),
      $._arguments,
    ),

    index_expr: $ => seq(
      field('object', $.primary_expr),
      '[', field('field', $._expression), ']',
    ),

    member_expr: $ => seq(
      field('object', $.primary_expr), ".",
      field('field', choice($.identifier, $.symbol, $.index))
    ),

    binary_expr: $ => choice(
      ...binary_expr($, false),
    ),

    // Current item reference in pipe context
    current_item: _ => '~',

    // Current key/index reference in pipe context
    current_index: _ => '~#',

    unary_expr: $ => prec.left(seq(
      field('operator', choice('not', '-', '+')),
      field('operand', $._expression),
    )),

    identifier: _ => {
      // ECMAScript 2023-compliant identifier regex:
      // const identifierRegex = /^[$_\p{ID_Start}][$_\u200C\u200D\p{ID_Continue}]*$/u;

      // 'alpha' and 'alphanumeric' here, copied from Tree-sitter JS grammar,
      // are not exactly the same as ECMA standard, which is a limitation of Tree-sitter RegEx
      const alpha = /[^\x00-\x1F\s\p{Zs}0-9:;`"'@#.,|^&<=>+\-*/\\%?!~()\[\]{}\uFEFF\u2060\u200B\u2028\u2029]|\\u[0-9a-fA-F]{4}|\\u\{[0-9a-fA-F]+\}/;
      const alphanumeric = /[^\x00-\x1F\s\p{Zs}:;`"'@#.,|^&<=>+\-*/\\%?!~()\[\]{}\uFEFF\u2060\u200B\u2028\u2029]|\\u[0-9a-fA-F]{4}|\\u\{[0-9a-fA-F]+\}/;
      return token(seq(alpha, repeat(alphanumeric)));
    },

    // JS Fn Parameter : Identifier | ObjectBinding | ArrayBinding, Initializer_opt
    // Supports: name, name?, name: type, name?: type, name = default, name: type = default
    parameter: $ => choice(
      seq(
        field('name', $.identifier),
        optional(field('optional', '?')),  // optional marker BEFORE type
        optional(seq(':', field('type', $._type_expr))),
        optional(seq('=', field('default', $._expression))),
      ),
      field('variadic', '...'),  // variadic marker (must be last parameter)
    ),

    // Named argument in function call: name: value
    named_argument: $ => seq(
      field('name', $.identifier),
      ':',
      field('value', $._expression),
    ),

    // fn with stam body
    fn_stam: $ => seq(
      optional(field('pub', 'pub')), // note: pub fn is only allowed at global level
      field('kind', choice('fn','pn')), field('name', $.identifier),
      '(', optional(field('declare', $.parameter)), repeat(seq(',', field('declare', $.parameter))), ')',
      // return type
      optional(field('type', $._type_expr)),
      '{', field('body', $.content), '}',
    ),

    // fn with expr body
    fn_expr_stam: $ => seq(
      optional(field('pub', 'pub')), // note: pub fn is only allowed at global level
      'fn', field('name', $.identifier),
      '(', field('declare', $.parameter), repeat(seq(',', field('declare', $.parameter))), ')',
      // return type
      optional(field('type', $._type_expr)),
      '=>', field('body', $._expression)
    ),

    // anonymous function
    fn_expr: $ => choice(
      seq('fn',
        '(', field('declare', $.parameter), repeat(seq(',', field('declare', $.parameter))), ')',
        // return type
        optional(field('type', $._type_expr)),
        '{', field('body', $.content), '}'
      ),
      // use prec.right so the expression body is consumed greedily
      prec.right(seq(
        '(', field('declare', $.parameter), repeat(seq(',', field('declare', $.parameter))), ')',
        // return type
        optional(field('type', $._type_expr)),
        '=>', field('body', $._expression)
      )),
    ),

    // use prec.right so the expression is consumed greedily
    // Single assignment: let x = expr
    // Positional decomposition: let a, b = expr
    // Named decomposition: let a, b at expr
    assign_expr: $ => prec.right(choice(
      // single variable assignment
      seq(
        field('name', $.identifier),
        optional(seq(':', field('type', $._type_expr))), '=', field('as', $._expression),
      ),
      // multi-variable decomposition: let a, b = expr OR let a, b at expr
      seq(
        field('name', $.identifier),
        repeat1(seq(',', field('name', $.identifier))),
        field('decompose', choice('=', 'at')),
        field('as', $._expression),
      ),
    )),

    let_expr: $ => seq(
      'let', field('declare', $.assign_expr)
    ),

    let_stam: $ => seq(
      'let', field('declare', $.assign_expr), repeat(seq(',', field('declare', $.assign_expr)))
    ),

    pub_stam: $ => seq(
      'pub', field('declare', $.assign_expr), repeat(seq(',', field('declare', $.assign_expr)))
    ),

    if_expr: $ => prec.right(seq(
      'if', '(', field('cond', $._expression), ')', field('then', $._expression),
      // 'else' clause is not optional for if_expr
      seq('else', field('else', $._expression)),
    )),

    if_stam: $ => prec.right(seq(
      'if', field('cond', $._expression), '{', field('then', $.content), '}',
      optional(seq('else', '{', field('else', $.content), '}')),
    )),

    // Loop variable binding with optional index and 'in' or 'at' keyword
    // Single variable: for v in expr
    // Indexed: for i, v in expr
    // Attribute iteration: for v at expr OR for k, v at expr
    loop_expr: $ => choice(
      // for value in | at expr
      seq(
        field('name', $.identifier), choice('in', 'at'), field('as', $._expression)
      ),
      // for key, value in | at expr
      seq(
        field('index', $.identifier), ',', field('name', $.identifier),
        choice('in', 'at'), field('as', $._expression)
      ),
    ),

    // use prec.right so the body expression is consumed greedily
    for_expr: $ => prec.right(seq(
      'for', '(', field('declare', $.loop_expr),
      repeat(seq(',', field('declare', $.loop_expr))), ')',
      field('then', $._expression),
    )),

    for_stam: $ => seq(
      'for', seq(field('declare', $.loop_expr), repeat(seq(',', field('declare', $.loop_expr)))),
      '{', field('then', $.content), '}'
    ),

    // while statement (procedural only)
    while_stam: $ => seq(
      'while', '(', field('cond', $._expression), ')',
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
      optional(field('value', $._expression)),
      optional(';')
    )),

    // var statement for mutable variables (procedural only)
    var_stam: $ => seq(
      'var', field('declare', $.assign_expr), repeat(seq(',', field('declare', $.assign_expr)))
    ),

    // assignment statement for mutable variables (procedural only)
    // use prec.right to prefer consuming expression when present
    assign_stam: $ => prec.right(seq(
      field('target', $.identifier), '=', field('value', $._expression),
      optional(';')
    )),

    // Type Definitions: ----------------------------------

    occurrence: $ => choice('?', '+', '*'),

    base_type: $ => built_in_types(true),

    _non_null_base_type: $ => built_in_types(false),

    list_type: $ => seq(
      // list cannot be empty
      '(', seq($._type_expr, repeat(seq(',', $._type_expr))), ')',
    ),

    array_type: $ => seq(
      '[', comma_sep($._type_expr), ']',
    ),

    map_type_item: $=> seq(
      field('name', choice($.identifier, $.symbol)), ':', field('as', $._type_expr)
    ),

    map_type: $ => seq('{',
      optional(seq($.map_type_item, repeat(seq(',', $.map_type_item)))), '}'
    ),

    attr_type: $ => seq(
      field('name', choice($.string, $.symbol, $.identifier)),
      ':', field('as', $._type_expr)
    ),

    content_type: $ => seq(
      $._type_expr,
      repeat(seq(',', $._type_expr)),
    ),

    element_type: $ => seq('<', $.identifier, _attr_content_type($), '>'),

    fn_param: $ => seq(
      // param type is required
      field('name', $.identifier), seq(':', field('type', $._type_expr)),
    ),

    fn_type: $ => seq(
      '(', optional(field('declare', $.fn_param)), repeat(seq(',', field('declare', $.fn_param))), ')',
      '->', field('type', $._type_expr),
    ),

    primary_type: $ => choice(
      $._non_null_literal, // non-null literal values; null is now a base type
      $.base_type,
      $.identifier,  // type reference
      $.list_type,
      $.array_type,
      $.map_type,
      $.element_type,
      $.fn_type,
    ),

    type_occurrence: $ => prec.right(seq(
      field('operand', $._type_expr),
      field('operator', $.occurrence),
    )),

    binary_type: $ => choice(
      ...type_pattern($._type_expr),
    ),

    _type_expr: $ => choice(
      $.primary_type,
      $.type_occurrence,
      $.binary_type,
    ),

    type_assign: $ => seq(field('name', $.identifier), '=', field('as', $._type_expr)),

    entity_type: $ => seq(
      'type', field('name', $.identifier), '<', _attr_content_type($), '>'
    ),

    object_type: $ => seq(
      'type', field('name', $.identifier), '{', _attr_content_type($), '}'
    ),

    type_stam: $ => seq(
      'type', field('declare', alias($.type_assign, $.assign_expr)),
      repeat(seq(',', field('declare', alias($.type_assign, $.assign_expr))))
    ),

    // top-level type defintions: type_stam | entity_type | object_type

    // Module Imports

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

    // ==================== String/Symbol Pattern Definitions ====================

    // Character classes for pattern matching
    pattern_char_class: _ => token(choice(
      '\\d',  // digit [0-9]
      '\\w',  // word [a-zA-Z0-9_]
      '\\s',  // whitespace
      '\\a',  // alpha [a-zA-Z]
    )),

    // Backslash-dot matches any character
    pattern_any: _ => '\\.',

    // Ellipsis matches zero or more of any character (shorthand for \.*)
    pattern_any_star: _ => '...',

    // Occurrence count for patterns: {n}, {n,}, {n,m}
    pattern_count: $ => choice(
      seq('{', $.integer, '}'),                        // exactly n
      seq('{', $.integer, ',', '}'),                   // n or more
      seq('{', $.integer, ',', $.integer, '}'),        // n to m
    ),

    // Primary pattern expression
    primary_pattern: $ => choice(
      $.string,                          // literal string "abc"
      $.pattern_char_class,              // \d, \w, \s, \a
      $.pattern_any,                     // \. (any character)
      $.pattern_any_star,                // ... (zero or more of any character)
      seq('(', $._pattern_expr, ')'),    // grouping
    ),

    // Pattern with occurrence modifiers: ?, +, *, {n}, {n,}, {n,m}
    pattern_occurrence: $ => prec.right(seq(
      field('operand', choice($.primary_pattern, $.pattern_negation)),
      field('operator', choice('?', '+', '*', $.pattern_count)),
    )),

    // Pattern negation: !pattern
    pattern_negation: $ => prec.right(seq(
      '!',
      field('operand', $.primary_pattern),
    )),

    // Pattern range: "a" to "z" -> [a-z]
    pattern_range: $ => prec.left('pattern_range', seq(
      field('left', $.string),
      'to',
      field('right', $.string),
    )),

    // Binary pattern expressions: | (union), & (intersection)
    binary_pattern: $ => choice(
      prec.left('pattern_union', seq(
        field('left', $._pattern_term),
        field('operator', '|'),
        field('right', $._pattern_expr),
      )),
      prec.left('pattern_intersect', seq(
        field('left', $._pattern_term),
        field('operator', '&'),
        field('right', $._pattern_expr),
      )),
    ),

    // Pattern term: single pattern unit (possibly with occurrence)
    _pattern_term: $ => choice(
      $.primary_pattern,
      $.pattern_occurrence,
      $.pattern_negation,
      $.pattern_range,
    ),

    // Pattern sequence: patterns concatenated
    pattern_seq: $ => prec.left('pattern_concat', repeat1($._pattern_term)),

    // Pattern expression (all pattern forms)
    _pattern_expr: $ => choice(
      $.pattern_seq,
      $.binary_pattern,
    ),

    // String pattern definition: string name = pattern
    string_pattern: $ => seq(
      'string',
      field('name', $.identifier),
      '=',
      field('pattern', $._pattern_expr),
    ),

    // Symbol pattern definition: symbol name = pattern
    symbol_pattern: $ => seq(
      'symbol',
      field('name', $.identifier),
      '=',
      field('pattern', $._pattern_expr),
    ),

  },
});

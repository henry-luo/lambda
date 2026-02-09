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

// need to exclude relational exprs in attr, optionally exclude pipe operators
function binary_expr($, in_attr, exclude_pipe = false) {
  let operand = in_attr ? choice($.primary_expr, $.unary_expr, alias($.attr_binary_expr, $.binary_expr))
                        : (exclude_pipe ? $._expression_no_pipe : $._expression);
  let ops = [
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
    ...(exclude_pipe ? [] : [
      ['|', 'pipe'],
      ['where', 'pipe'],  // same precedence as | for left-to-right chaining
      // Pipe-to-file operators (procedural only) - lower precedence than | and where
      ['|>', 'pipe_file'],
      ['|>>', 'pipe_file'],  
    ]),
    ['&', 'set_intersect'],
    ['!', 'set_exclude'],  // set1 ! set2, elements in set1 but not in set2.
    ['is', 'is_in'],
    ['in', 'is_in'],
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
    $.call_expr,
    $.index_expr,
    $.member_expr,
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
    'pipe_file',  // |> and |>> - lowest binary precedence
    $.if_expr,
    $.for_expr,
    $.let_expr,
    $.fn_expr,
    $.assign_expr,
    $.assign_stam,
  ],
  [
    $.fn_type,
    $.type_occurrence,
    $.primary_type,
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

  conflicts: $ => [],

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
      repeat(choice(
        /[^'\\\n]+/,  // any chars except ', \, and newline
        /\\['\\\/bfnrt]/,  // simple escapes
        /\\u[0-9a-fA-F]{4}/,  // \uXXXX
        /\\u\{[0-9a-fA-F]+\}/,  // \u{X...}
      )),
      "'",
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
      $.raise_stam,
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
      $.raise_expr,
    ),

    // Expression without pipe operators (|, where) - used in for loop iteration expression
    _expression_no_pipe: $ => choice(
      $.primary_expr,
      $.unary_expr,
      $.binary_expr_no_pipe,
      $.let_expr,
      $.if_expr,
      $.for_expr,
      $.fn_expr,
      $.raise_expr,
    ),

    // raise expression - raises an error in functional context
    raise_expr: $ => prec.right(seq(
      'raise', field('value', $._expression)
    )),

    // Binary expression without pipe operators
    binary_expr_no_pipe: $ => choice(
      ...binary_expr($, false, true),
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
      $.index_expr,
      $.path_expr,   // /, ., or .. paths with optional segment
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

    // Path root: / for absolute file paths
    path_root: _ => '/',
    
    // Path self: . for relative paths (current directory)
    path_self: _ => '.',
    
    // Path parent: .. for parent directory
    path_parent: _ => '..',
    
    // Path expression: /, ., or .. optionally followed by a field
    // This allows /etc, .test, ..parent, /, ., .. as path expressions
    path_expr: $ => prec.right(seq(
      choice($.path_root, $.path_self, $.path_parent),
      optional(field('field', choice($.identifier, $.symbol, $.index, $.path_wildcard, $.path_wildcard_recursive)))
    )),

    // Member access
    member_expr: $ => seq(
      field('object', $.primary_expr), ".",
      field('field', choice($.identifier, $.symbol, $.index, $.path_wildcard, $.path_wildcard_recursive))
    ),

    // Path wildcards for glob patterns
    path_wildcard: _ => token('*'),               // single wildcard: match one segment
    path_wildcard_recursive: _ => token('**'),    // recursive wildcard: match zero or more segments

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
      // return type with optional error type: T or T^E or T^
      optional(field('type', $.return_type)),
      '{', field('body', $.content), '}',
    ),

    // fn with expr body
    fn_expr_stam: $ => seq(
      optional(field('pub', 'pub')), // note: pub fn is only allowed at global level
      'fn', field('name', $.identifier),
      '(', field('declare', $.parameter), repeat(seq(',', field('declare', $.parameter))), ')',
      // return type with optional error type: T or T^E or T^
      optional(field('type', $.return_type)),
      '=>', field('body', $._expression)
    ),

    // anonymous function
    fn_expr: $ => choice(
      seq('fn',
        '(', field('declare', $.parameter), repeat(seq(',', field('declare', $.parameter))), ')',
        // return type with optional error type: T or T^E or T^
        optional(field('type', $.return_type)),
        '{', field('body', $.content), '}'
      ),
      // use prec.right so the expression body is consumed greedily
      prec.right(seq(
        '(', field('declare', $.parameter), repeat(seq(',', field('declare', $.parameter))), ')',
        // return type with optional error type: T or T^E or T^
        optional(field('type', $.return_type)),
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
    // Use _expression_no_pipe so 'where' is not consumed as binary operator
    loop_expr: $ => choice(
      // for value in | at expr
      seq(
        field('name', $.identifier), choice('in', 'at'), field('as', $._expression_no_pipe)
      ),
      // for key, value in | at expr
      seq(
        field('index', $.identifier), ',', field('name', $.identifier),
        choice('in', 'at'), field('as', $._expression_no_pipe)
      ),
    ),

    // let clause within for: let name = expr
    // Use _expression_no_pipe so subsequent 'where' is not consumed
    for_let_clause: $ => seq(
      'let', field('name', $.identifier), '=', field('value', $._expression_no_pipe)
    ),

    // where clause: where expr
    // Use prec.dynamic to prefer this over binary 'where' in for context
    for_where_clause: $ => prec.dynamic(10, seq(
      'where', field('cond', $._expression)
    )),

    // order by clause: order by expr [asc|desc] [, expr [asc|desc], ...]
    // Use _expression_no_pipe so limit/offset are not consumed
    order_spec: $ => seq(
      field('expr', $._expression_no_pipe),
      optional(field('dir', choice('asc', 'ascending', 'desc', 'descending')))
    ),

    for_order_clause: $ => seq(
      'order', 'by', field('spec', $.order_spec),
      repeat(seq(',', field('spec', $.order_spec)))
    ),

    // group by clause: group by expr [, expr, ...] as name
    for_group_clause: $ => seq(
      'group', 'by', field('key', $._expression),
      repeat(seq(',', field('key', $._expression))),
      'as', field('name', $.identifier)
    ),

    // limit clause: limit expr
    for_limit_clause: $ => seq(
      'limit', field('count', $._expression)
    ),

    // offset clause: offset expr
    for_offset_clause: $ => seq(
      'offset', field('count', $._expression)
    ),

    // use prec.right so the body expression is consumed greedily
    for_expr: $ => prec.right(seq(
      'for', '(',
      field('declare', $.loop_expr),
      repeat(seq(',', field('declare', $.loop_expr))),
      // optional let clauses (comma-separated after declarations)
      repeat(seq(',', field('let', $.for_let_clause))),
      // optional clauses in order
      optional(field('where', $.for_where_clause)),
      optional(field('group', $.for_group_clause)),
      optional(field('order', $.for_order_clause)),
      optional(field('limit', $.for_limit_clause)),
      optional(field('offset', $.for_offset_clause)),
      ')',
      field('then', $._expression),
    )),

    for_stam: $ => seq(
      'for',
      field('declare', $.loop_expr),
      repeat(seq(',', field('declare', $.loop_expr))),
      // optional let clauses
      repeat(seq(',', field('let', $.for_let_clause))),
      // optional clauses in order
      optional(field('where', $.for_where_clause)),
      optional(field('group', $.for_group_clause)),
      optional(field('order', $.for_order_clause)),
      optional(field('limit', $.for_limit_clause)),
      optional(field('offset', $.for_offset_clause)),
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

    // raise statement (procedural only) - raises an error to caller
    // use prec.right to prefer consuming expression when present
    raise_stam: $ => prec.right(seq(
      'raise',
      field('value', $._expression),
      optional(';')
    )),

    // var statement for mutable variables (procedural only)
    var_stam: $ => seq(
      'var', field('declare', $.assign_expr), repeat(seq(',', field('declare', $.assign_expr)))
    ),

    // assignment statement for mutable variables (procedural only)
    // use prec.right to prefer consuming expression when present
    assign_stam: $ => seq(
      field('target', $.identifier), '=', field('value', $._expression),
      optional(';')
    ),

    // Type Definitions: ----------------------------------

    // Occurrence modifiers for types: ?, +, *, [n], [n, m], [n+]
    occurrence: $ => choice('?', '+', '*', $.occurrence_count),

    // Occurrence count: [n] (exact), [n, m] (range), [n+] (unbounded)
    occurrence_count: $ => choice(
      seq('[', $.integer, ']'),                      // exactly n: T[5]
      seq('[', $.integer, ',', $.integer, ']'),      // n to m: T[2, 5]
      seq('[', $.integer, '+', ']'),                 // n or more: T[3+]
    ),

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
      'fn',
      optional(seq(
        '(', optional(field('declare', $.fn_param)), repeat(seq(',', field('declare', $.fn_param))), ')',
      )),
      field('type', $._type_expr),
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
      $.error_union_type,
    ),

    // Error union type: T^ means T | error
    // Used in parameters and let bindings to accept values that may be errors
    // Note: In return types, use T^. instead (to avoid grammar conflicts)
    // Use lower precedence than fn_type to avoid conflict with fn_type's return type
    error_union_type: $ => prec.left(-1, seq(
      field('ok', $._type_expr),
      '^'
    )),

    // Return type with optional error type: T or T^E or T^.
    // T^E means function returns T on success, E on error
    // T^. means function may return any error (error type inferred)
    // Use '^.' instead of bare '^' to avoid conflict with map_type consuming function body
    return_type: $ => prec.right(seq(
      field('ok', $._type_expr),
      optional(choice(
        // T^E - explicit error type
        seq('^', field('error', $._type_expr)),
        // T^. - any error (wildcard, like . in string patterns)
        seq('^', '.')
      ))
    )),

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

    // Occurrence count for patterns: [n], [n+], [n, m]
    pattern_count: $ => choice(
      seq('[', $.integer, ']'),                        // exactly n: "a"[3]
      seq('[', $.integer, '+', ']'),                   // n or more: "a"[2+]
      seq('[', $.integer, ',', $.integer, ']'),        // n to m: "a"[2, 5]
    ),

    // Primary pattern expression
    primary_pattern: $ => choice(
      $.string,                          // literal string "abc"
      $.pattern_char_class,              // \d, \w, \s, \a
      $.pattern_any,                     // \. (any character)
      $.pattern_any_star,                // ... (zero or more of any character)
      seq('(', $._pattern_expr, ')'),    // grouping
    ),

    // Pattern with occurrence modifiers: ?, +, *, [n], [n+], [n, m]
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

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

// need to exclude relational exprs in attr (to avoid conflicts with element tags)
// pipe operators are always included
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
    ['^', 'binary_pow', 'right'],
    ['==', 'binary_eq'],
    ['!=', 'binary_eq'],
    // Relational operators - excluded in attr to avoid element tag conflicts
    ...(in_attr ? [] :
      [['<', 'binary_relation'],
      ['<=', 'binary_relation'],
      ['>=', 'binary_relation'],
      ['>', 'binary_relation']]),
    ['and', 'logical_and'],
    ['or', 'logical_or'],
    ['to', 'range_to'],
    // Pipe operators - always included (even in attr context)
    ['|', 'pipe'],
    ['that', 'pipe'],  // filter operator: items that ~ > 0
    ['|>', 'pipe_file'],
    ['|>>', 'pipe_file'],
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
    // $._expr,
  ],

  inline: $ => [
    $._non_null_literal,
    $._parenthesized_expr,
    $._arguments,
    $._number,
    $._datetime,
  ],

  conflicts: $ => [
    [$._expr, $.member_expr],
    [$.list, $.if_expr],                           // if(expr) could start list (for fn_expr) or if_expr
    [$._quantified_type, $.occurrence_type],       // unary_type + [n] could be occurrence or end of type
    [$._compound_type, $.concat_type],             // _quantified_type could be complete or start of concat
    [$._compound_type, $.constrained_type],        // _quantified_type could be complete or base of constrained
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
    $.match_expr,
    $.for_expr,
    $.let_expr,
    $.assign_expr,
    $.assign_stam,
  ],
  [
    $.unary_type,            // atomic types (primary_type | negation_type) - tightest
    $.binary_type,           // alternation (|, &, !)
    $.fn_type,               // fn binds loosest: fn int+ means fn (int+)
  ]],

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

    // Note: 'null' is now part of $.base_type, no separate rule needed
    true: _ => 'true',
    false: _ => 'false',
    inf: _ => 'inf',
    nan: _ => 'nan',

    // Containers: list, array, map, element

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
      $.match_expr,
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
      '(', $._expr, repeat(seq(',', $._expr)), ')'
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
      field('as', $._expr),
    ),

    map: $ => seq(
      // $._expr for dynamic map item
      '{', comma_sep(choice($.map_item, $._expr)), '}',
    ),

    array: $ => seq(
      '[', comma_sep($._expr), ']',
    ),

    range: $ => seq(
      $._expr, 'to', $._expr,
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
    ),

    // Attribute name with optional namespace prefix: name or ns.name
    attr_name: $ => choice(
      $.ns_identifier,
      $.string,
      $.symbol,
      $.identifier,
    ),

    attr: $ => seq(
      field('name', $.attr_name),
      ':', field('as', $._attr_expr)
    ),

    // Namespaced identifier: ns.name (no spaces around dot)
    ns_identifier: $ => seq(
      field('ns', $.identifier),
      token.immediate('.'),
      field('name', $.identifier),
    ),

    element: $ => seq('<',
      choice($.ns_identifier, $.symbol, $.identifier),
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
      '(', $._expr, ')',
    ),

    _expr: $ => choice(
      $.primary_expr,
      $.unary_expr,
      $.spread_expr,
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
      $.base_type,  // includes null
      $.identifier,
      $.index_expr,
      $.path_expr,   // /, ., or .. paths with optional segment
      $.member_expr,
      $.call_expr,
      $._parenthesized_expr,
      $.fn_expr,    // arrow fn: (params) => expr - colocated with list for GLR
      $.current_item,   // ~ for pipe context
      $.current_index,  // ~# for pipe key/index
    )),

    spread_argument: $ => seq('...', $._expr),

    _arguments: $ => seq(
      '(', comma_sep(optional(
      field('argument', choice($.named_argument, $._expr, $.spread_argument)))), ')',
    ),

    import: _ => token('import'),

    call_expr: $ => prec.right(100, seq(
      field('function', choice($.primary_expr, $.import)),
      $._arguments,
      optional(field('propagate', '?')),
    )),

    index_expr: $ => prec.right(100, seq(
      field('object', $.primary_expr),
      '[', field('field', $._expr), ']',
    )),

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
      field('operator', choice('not', '-', '+', '^')),
      field('operand', $._expr),
    )),

    // Spread expression: *expr - spreads array/list items into container
    spread_expr: $ => prec.left(seq(
      field('operator', '*'),
      field('operand', $._expr),
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
        optional(seq('=', field('default', $._expr))),
      ),
      field('variadic', '...'),  // variadic marker (must be last parameter)
    ),

    // Named argument in function call: name: value
    named_argument: $ => seq(
      field('name', $.identifier),
      ':',
      field('value', $._expr),
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
      '=>', field('body', $._expr)
    ),

    // Anonymous Function (arrow expression)
    // Three forms:
    //   Typed params:   (a: int, b: string) => expr  (uses parameter nodes)
    //   Untyped params: (a, b) => expr               (uses list, AST builder reinterprets)
    //   No params:      () => expr                   (empty parens)
    fn_expr: $ => prec.right(choice(
      // Typed params: (a: int, b: string) => expr
      prec.dynamic(1, seq(
        '(', field('declare', $.parameter), repeat(seq(',', field('declare', $.parameter))), ')',
        optional(field('type', $.return_type)), '=>', field('body', $._expr)
      )),
      // Untyped params via list: (a, b) => expr
      seq($.list, optional(field('type', $.return_type)), '=>', field('body', $._expr)),
      // No params: () => expr
      seq('(', ')', optional(field('type', $.return_type)), '=>', field('body', $._expr)),
    )),

    // use prec.right so the expression is consumed greedily
    // Single assignment: let x = expr
    // Error destructuring: let a^err = expr
    // Positional decomposition: let a, b = expr
    // Named decomposition: let a, b at expr
    assign_expr: $ => prec.right(choice(
      // error destructuring: let name^error_name = expr
      seq(
        field('name', $.identifier),
        '^', field('error', $.identifier),
        '=', field('as', $._expr),
      ),
      // single variable assignment
      seq(
        field('name', $.identifier),
        optional(seq(':', field('type', $._type_expr))), '=', field('as', $._expr),
      ),
      // multi-variable decomposition: let a, b = expr OR let a, b at expr
      seq(
        field('name', $.identifier),
        repeat1(seq(',', field('name', $.identifier))),
        field('decompose', choice('=', 'at')),
        field('as', $._expr),
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
      'if', '(', field('cond', $._expr), ')', field('then', $._expr),
      // 'else' clause is not optional for if_expr
      seq('else', field('else', $._expr)),
    )),

    if_stam: $ => prec.right(seq(
      'if', field('cond', $._expr), '{', field('then', $.content), '}',
      optional(choice(
        seq('else', field('else', $.if_stam)),
        seq('else', '{', field('else', $.content), '}'),
      )),
    )),

    // Match expression — unified form with required braces
    // match expr { case_arms }
    // Each arm can be expression form (case T: expr) or statement form (case T { stmts })
    match_expr: $ => seq(
      'match', field('scrutinee', $._expr),
      '{',
      repeat1(choice($.match_arm_expr, $.match_arm_stam, $.match_default_expr, $.match_default_stam)),
      '}'
    ),

    match_arm_expr: $ => prec.right(seq(
      'case', field('pattern', $._type_expr),
      ':', field('body', $._expr)
    )),

    match_default_expr: $ => prec.right(seq(
      'default', ':', field('body', $._expr)
    )),

    match_arm_stam: $ => seq(
      'case', field('pattern', $._type_expr),
      '{', field('body', $.content), '}'
    ),

    match_default_stam: $ => seq(
      'default', '{', field('body', $.content), '}'
    ),

    // Loop variable binding with optional index and 'in' or 'at' keyword
    // Single variable: for v in expr
    // Indexed: for i, v in expr
    // Attribute iteration: for v at expr OR for k, v at expr
    loop_expr: $ => choice(
      // for value in | at expr
      seq(
        field('name', $.identifier), choice('in', 'at'), field('as', $._expr)
      ),
      // for key, value in | at expr
      seq(
        field('index', $.identifier), ',', field('name', $.identifier),
        choice('in', 'at'), field('as', $._expr)
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

    // group by clause: group by expr [, expr, ...] as name
    for_group_clause: $ => seq(
      'group', 'by', field('key', $._expr),
      repeat(seq(',', field('key', $._expr))),
      'as', field('name', $.identifier)
    ),

    // limit clause: limit expr
    for_limit_clause: $ => seq(
      'limit', field('count', $._expr)
    ),

    // offset clause: offset expr
    for_offset_clause: $ => seq(
      'offset', field('count', $._expr)
    ),

    // shared for clauses: where, group, order, limit, offset in any order
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
      // optional clauses (where, group, order, limit, offset) in any order
      optional($.for_clauses),
      ')',
      field('then', $._expr),
    )),

    for_stam: $ => seq(
      'for',
      field('declare', $.loop_expr),
      repeat(seq(',', field('declare', $.loop_expr))),
      // optional let clauses
      repeat(seq(',', field('let', $.for_let_clause))),
      // optional clauses (where, group, order, limit, offset) in any order
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
    // use prec.right to prefer consuming expression when present
    raise_stam: $ => prec.right(seq(
      'raise',
      field('value', $._expr),
      optional(';')
    )),

    // var statement for mutable variables (procedural only)
    var_stam: $ => seq(
      'var', field('declare', $.assign_expr), repeat(seq(',', field('declare', $.assign_expr)))
    ),

    // assignment statement for mutable variables (procedural only)
    // use prec.right to prefer consuming expression when present
    assign_stam: $ => seq(
      field('target', $.identifier), '=', field('value', $._expr),
      optional(';')
    ),

    // Type Definitions: ----------------------------------

    // Occurrence modifiers for types: ?, +, *, [n], [n, m], [n+]
    occurrence: $ => choice('?', '+', '*', $.occurrence_count),

    // Occurrence count: [n] (exact), [n, m] (range), [n+] (unbounded)
    // Higher precedence than primary_type to prefer occurrence over array_type
    occurrence_count: $ => prec(2, choice(
      seq('[', $.integer, ']'),                      // exactly n: T[5]
      seq('[', $.integer, ',', $.integer, ']'),      // n to m: T[2, 5]
      seq('[', $.integer, '+', ']'),                 // n or more: T[3+]
    )),

    // Built-in types as reserved keywords
    base_type: _ => prec(1, choice(
      'null', 'any', 'error', 'bool', 'int64', 'int', 'float', 'decimal', 'number',
      'datetime', 'date', 'time', 'symbol', 'string', 'binary', 'range',
      'list', 'array', 'map', 'element', 'entity', 'object', 'type', 'function'
    )),

    // list_type for tuple types and pattern grouping
    // e.g. (int, string) for tuple, ("a" to "z")+ for grouped pattern with occurrence
    // AST builder rejects comma-separated multi-element list_type in pattern context.
    list_type: $ => prec.dynamic(2, seq(
      // list cannot be empty
      '(', seq($._type_expr, repeat(seq(',', $._type_expr))), ')',
    )),

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

    // Range type: literal 'to' literal — supports integer ranges as types
    // e.g. 1 to 10, 0 to 255
    range_type: $ => prec.left('range_to', seq(
      field('start', $._non_null_literal), 'to', field('end', $._non_null_literal),
    )),

    primary_type: $ => choice(
      $.range_type,        // range_type first to ensure "a" to "z" is parsed as range
      $._non_null_literal, // non-null literal values; null is now a base type
      $.base_type,
      $.identifier,  // type reference / pattern reference
      $.list_type,
      $.array_type,
      $.map_type,
      $.element_type,
      $.fn_type,
      // String/symbol pattern atoms (unified into type system)
      $.pattern_char_class,     // \d, \w, \s, \a
      $.pattern_any,            // \. (any character)
    ),

    // Unary type: primary type or prefix-modified type (before occurrence)
    // This is what occurrence modifiers can apply to.
    unary_type: $ => choice(
      $.primary_type,
      $.negation_type,       // !T - prefix negation
    ),

    // Quantified: unary type with optional occurrence modifier
    // Structural level between unary_type and compound types
    _quantified_type: $ => choice(
      $.occurrence_type,
      $.unary_type,
    ),

    // Compound type: constrained, concat, or simple quantified
    // Structural level between quantified and binary types
    _compound_type: $ => choice(
      $.constrained_type,
      $.concat_type,
      $._quantified_type,
    ),

    // Occurrence applied to unary type: T?, T+, T*, T[n]
    // No chaining allowed (like regex) - use explicit grouping: (T*)[2]
    // Use prec.dynamic(1) to prefer occurrence over concat_type continuation.
    occurrence_type: $ => prec.dynamic(1, prec.right(seq(
      field('operand', $.unary_type),
      field('operator', $.occurrence),
    ))),

    binary_type: $ => choice(
      ...type_pattern($._type_expr),
    ),

    // ====== Type Concatenation (for string/symbol patterns) ======
    // Type concatenation: whitespace-separated sequence of type terms.
    // e.g. \d[3] "-" \d[3] "-" \d[4]
    // Only valid inside string/symbol pattern definitions; AST builder rejects elsewhere.
    // Terms are _quantified_type (unary_type possibly with occurrence).
    concat_type: $ => prec.left(prec.dynamic(-1, seq(
      $._quantified_type,
      repeat1($._quantified_type),
    ))),

    // Prefix negation: !T (for string/symbol patterns: !\d)
    // Validated in AST builder for context-appropriate usage.
    negation_type: $ => prec.right(seq(
      '!', field('operand', $.primary_type),
    )),

    // Unified type expression - layered hierarchy
    // Structural precedence (tightest to loosest):
    //   unary_type > occurrence_type > constrained/concat > binary > error_union
    _type_expr: $ => choice(
      $._compound_type,        // covers unary, occurrence, constrained, concat
      $.binary_type,           // alternation: T | U, T & U, T ! U
      $.error_union_type,      // T^ error union
    ),

    // Constrained type: base_type that (constraint_expr)
    // e.g. int that (5 < ~ < 10), string that (len(~) > 0)
    // The constraint uses ~ to refer to the value being checked
    // Parentheses required to avoid grammar ambiguity with index expressions
    constrained_type: $ => seq(
      field('base', $._quantified_type),
      'that',
      '(',
      field('constraint', $._expr),
      ')',
    ),

    // Error union type: T^ means T | error
    // Used in parameters, let bindings, and return types
    // Use lower precedence than fn_type to avoid conflict with fn_type's return type
    error_union_type: $ => prec.left(-1, seq(
      field('ok', $._type_expr),
      '^'
    )),

    // Simple error type pattern for return types
    // Allows: error, identifier, or union of these (E1 | E2)
    // This restriction avoids ambiguity with map_type in fn bodies: T^ { ... }
    error_type_pattern: $ => seq(
      field('type', choice('error', $.identifier)),
      repeat(seq('|', field('type', choice('error', $.identifier))))
    ),

    // Return type with optional error type: T or T^ or T^E
    // T^ means function may return any error (shorthand for T | error)
    // T^E means function returns T on success, E on error (E must be simple)
    return_type: $ => prec.right(seq(
      field('ok', $._type_expr),
      optional(seq(
        '^',
        optional(field('error', $.error_type_pattern))
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

    _import_stam: $ => choice(
      seq('import', $.import_module, repeat(seq(',', $.import_module))),
      $.namespace_decl,
    ),

    // Namespace declaration: namespace ns1 : 'url', ns2 : "url", ns3: path, ...;
    namespace_decl: $ => seq(
      'namespace',
      $.namespace_binding,
      repeat(seq(',', $.namespace_binding)),
    ),

    namespace_binding: $ => seq(
      field('prefix', $.identifier),
      ':',
      field('uri', choice($.string, $.symbol, $.identifier)),
    ),

    // ==================== String/Symbol Pattern Definitions ====================
    // Pattern atoms are unified into the type system. String/symbol pattern bodies
    // use _type_expr directly. The AST builder validates that only pattern-valid
    // constructs appear inside pattern definitions.

    // Character classes for pattern matching
    pattern_char_class: _ => token(choice(
      '\\d',  // digit [0-9]
      '\\w',  // word [a-zA-Z0-9_]
      '\\s',  // whitespace
      '\\a',  // alpha [a-zA-Z]
    )),

    // Backslash-dot matches any character
    pattern_any: _ => '\\.',

    // String pattern definition: string name = type_expr
    // type_expr now includes concat_type for pattern concatenation.
    string_pattern: $ => prec.right(seq(
      'string',
      field('name', $.identifier),
      '=',
      field('pattern', $._type_expr),
    )),

    // Symbol pattern definition: symbol name = type_expr
    // type_expr now includes concat_type for pattern concatenation.
    symbol_pattern: $ => prec.right(seq(
      'symbol',
      field('name', $.identifier),
      '=',
      field('pattern', $._type_expr),
    )),

  },
});

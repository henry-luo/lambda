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

const linebreak = /\r\n|\n/;
const decimal_digits = /\d+/;
const integer_literal = seq(choice('0', seq(/[1-9]/, optional(decimal_digits))));
const exponent_part = seq(choice('e', 'E'), optional(choice('+', '-')), decimal_digits);
const float_literal = choice(
  seq(integer_literal, '.', decimal_digits, optional(exponent_part)),
  seq('.', decimal_digits, optional(exponent_part)),
  seq(integer_literal, exponent_part),
);
const decimal_literal = choice(
  seq(integer_literal, '.', decimal_digits),
  seq('.', decimal_digits),
);

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
    ['**', 'binary_pow', 'right'],
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
    $._key
  ],

  conflicts: $ => [
    [$._expr, $.member_expr],
    [$.dotted_name, $.primary_expr],               // identifier.identifier: shift for dotted_name vs reduce to primary_expr
    [$._expr, $.parent_expr],                      // expr .. could end expr or start parent access
    [$._expr, $.query_expr],                       // expr ? or .? could end expr or start query
    [$.list, $.if_expr],                           // if(expr) could start list (for fn_expr) or if_expr
    [$.unary_type, $.occurrence_type],              // primary_type + [n] could be occurrence or end of type
    [$._type_expr, $._string_type_expr],            // type_assign: unary_type could be _type_expr or _string_type_expr
    [$._string_type_expr, $.concat_type],            // unary_type could be complete _string_type_expr or start of concat_type
    [$.range_type, $.primary_type],                // literal could be complete primary_type or start of range_type
  ],

  precedences: $ => [[
    $.fn_expr_stam,
    $.call_expr,
    $.index_expr,
    $.member_expr,
    $.parent_expr,
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
    'pipe_file',  // |> and |>> - lowest binary precedence
    $.if_expr,
    $.match_expr,
    $.for_expr,
    $.let_expr,
    $.assign_expr,
    $.assign_stam,
  ],
  [
    $.unary_type,         // tight unary types 
    $.concat_type,        // in regex, concatenation has higher precedence than alternation, so concat_type is tighter than binary_type
    $.binary_type,        // alternation (|, &, !)
    $.negation_type,      // A ! B has higher precedence than A (!B)
    $._type_expr,   
    $.return_type,   
    $.fn_type,            // fn binds loosest: fn int+ means fn (int+)
  ],
  [$.attr_binary_expr, $._attr_expr]
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
      repeat(choice(
        /[^'\\\n]+/,  // any chars except ', \, and newline
        /\\['\\\/bfnrt]/,  // simple escapes
        /\\u[0-9a-fA-F]{4}/,  // \uXXXX
        /\\u\{[0-9a-fA-F]+\}/,  // \u{X...}
      )),
      "'",
    )),

    // binary token: b'...' containing hex or base64 data
    // Actual parsing done by AST builder
    binary: _ => token(seq("b'", repeat(/[^']/), "'")),

    _number: $ => choice($.integer, $.float, $.decimal),

    integer: _ => token(integer_literal),

    float: _ => token(float_literal),

    decimal: $ => {
      // no e-notation for decimal, following JS bigint
      return token( seq(choice(decimal_literal, integer_literal), choice('n','N')) );
    },

    // datetime token: t'...' containing date/time text
    // Actual parsing done by AST builder via datetime_parse()
    datetime: _ => token(seq( "t'", repeat(choice(/[0-9]/, /[:\-+.tTzZ ]/)), "'" )),

    // Note: 'null' is now part of $.base_type, no separate rule needed
    // named_value combines true/false/inf/nan into a single token to reduce SYMBOL_COUNT
    named_value: _ => token(choice('true', 'false', 'inf', 'nan')),

    // Containers: list, array, map, element

    // expr statements that need ';'
    _expr_stam: $ => choice(
      $.let_stam,
      $.fn_expr_stam,
      $.type_stam,
    ),

    _content_expr: $ => choice(
      repeat1(choice($.string, $.map, $.element)),
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
      $.break_stam,
      $.continue_stam,
      $.return_stam,
      $.raise_stam,
      $.var_stam,
      $.assign_stam,
      prec.right('statement_end', seq($._content_expr, choice(token(prec(10, /\r\n|\n/)), ';'))),
    ),

    content: $ => choice(
      seq(
        repeat1($._statement),
        optional($._content_expr)
      ),
      $._content_expr
    ),

    list: $ => seq( '(', $._expr, repeat(seq(',', $._expr)), ')' ),

    // Literals and Containers
    _non_null_literal: $ => choice(
      $._number,
      $.string,
      $.symbol,
      $.datetime,
      $.binary,
      $.named_value,
    ),

    _key: $ => choice($.dotted_name, $.symbol, $.identifier, $.base_type, '*'),

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

    // Attribute name
    attr_name: $ => $._key,

    attr: $ => seq( field('name', $.attr_name), ':', field('as', $._attr_expr) ),

    // Dotted name: arbitrary depth dotted segments
    // Each segment is an identifier or symbol: a.b.'c'.d
    // prec(50) matches primary_expr so shift-reduce becomes a real GLR conflict
    dotted_name: $ => prec.left(50, seq(
      choice($.identifier, $.symbol),
      repeat1(seq('.', choice($.identifier, $.symbol))),
    )),

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
      '(', $._expr, ')',
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
      $._number,
      $.datetime,
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
      $.dotted_name,  // a.b, svg.rect — lower priority than member_expr
      $.parent_expr,  // expr.. for parent access shorthand
      $.call_expr,
      $.query_expr,         // expr?T or expr.?T - query by type
      $._parenthesized_expr,
      $.fn_expr,    // arrow fn: (params) => expr - colocated with list for GLR
      $.current_expr,   // ~ or ~# for pipe context
      $.variadic,       // ... (to prevent ... being parsed as .. + .)
    )),

    _arguments: $ => seq(
      '(', comma_sep( field('argument', choice($.named_argument, $._expr)) ), ')',
    ),

    call_expr: $ => prec.right(100, seq(
      field('function', choice($.primary_expr, 'import')),
      $._arguments,
      optional(field('propagate', '^')),
    )),

    index_expr: $ => prec.right(100, seq(
      field('object', $.primary_expr),
      '[', field('field', $._expr), ']',
    )),

    // Query expression: expr?T (recursive) or expr.?T (direct)
    query_expr: $ => seq(
      field('object', $.primary_expr),
      field('op', choice('?', '.?')),
      field('query', $.primary_type),
    ),

    // Path prefix: /, ., or .. for path expressions
    // Combines path_root, path_self, path_parent into single token for path_expr
    _path_prefix: _ => token(choice('/', '.', '..')),

    // Variadic marker: ... (higher priority than path_parent)
    variadic: _ => token(prec(2, '...')),

    // Path parent: .. for parent directory (kept separate for parent_expr)
    path_parent: _ => '..',

    // Path expression: /, ., or .. optionally followed by a field
    // This allows /etc, .test, ..parent, /, ., .. as path expressions
    path_expr: $ => prec.right(seq(
      $._path_prefix,
      optional(field('field', choice($.identifier, $.symbol, $.integer, $.path_wildcard, $.base_type)))
    )),

    // Member access — prec.dynamic(1) ensures GLR parser prefers member_expr
    // over path_expr when both are viable (e.g., after a comment disrupts lookahead)
    member_expr: $ => prec.dynamic(1, seq(
      field('object', $.primary_expr), '.',
      field('field', choice($.identifier, $.symbol, $.integer, $.path_wildcard, $.base_type))
    )),

    // Parent access: expr.. for .parent, expr.._.. for .parent.parent
    parent_expr: $ => seq(
      field('object', $.primary_expr),
      $.path_parent,                     // .. for parent access
      repeat(seq('_', $.path_parent))   // _.. for additional parent levels
    ),

    // Path wildcard: * (single segment) or ** (recursive, zero or more segments)
    path_wildcard: _ => token(choice('**', '*')),

    binary_expr: $ => choice(
      ...binary_expr($, false),
    ),

    // Current item (~) or key/index (~#) reference in pipe context
    current_expr: _ => token(choice('~#', '~')),

    // Unary expression: includes not, !, -, +, ^, * (spread)
    unary_expr: $ => prec.left(seq(
      field('operator', choice('not', '!', '-', '+', '^', '*')),
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
        field('name', choice($.identifier, $.symbol)),
        optional(field('optional', '?')),  // optional marker BEFORE type
        optional(seq(':', field('type', $._type_expr))),
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
        field('name', choice($.identifier, $.symbol)),
        '^', field('error', choice($.identifier, $.symbol)),
        '=', field('as', $._expr),
      ),
      // single variable assignment
      seq(
        field('name', choice($.identifier, $.symbol)),
        optional(seq(':', field('type', $._type_expr))), '=', field('as', $._expr),
      ),
      // multi-variable decomposition: let a, b = expr OR let a, b at expr
      seq(
        field('name', choice($.identifier, $.symbol)),
        repeat1(seq(',', field('name', choice($.identifier, $.symbol)))),
        field('decompose', choice('=', 'at')),
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
    if_stam: $ => prec.right(seq(
      'if', field('cond', $._expr),
      '{', field('then', $.content), '}',
      optional(seq('else', choice(
        prec.dynamic(1, seq('{', field('else', $.content), '}')),
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
      'case', field('pattern', $._type_expr),
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
      field('then', $._expr),
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

    // Built-in types as reserved keywords
    // _base_type_kw combines 20 keywords only used via base_type into a single token,
    // reducing SYMBOL_COUNT by 19. Keywords also used standalone elsewhere
    // ('error', 'type', 'string', 'symbol') remain as separate keywords.
    _base_type_kw: _ => token(prec(1, choice(
      'null', 'any', 'bool', 'int64', 'int', 'float', 'decimal', 'number',
      'datetime', 'date', 'time', 'binary', 'range',
      'list', 'array', 'map', 'element', 'entity', 'object', 'function'
    ))),

    base_type: $ => prec(1, choice(
      $._base_type_kw,
      'error', 'type', 'string', 'symbol'
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

    attr_type: $ => prec(1, seq(
      field('name', choice($.symbol, $.identifier)),
      ':', field('as', $._type_expr),
      optional(seq('=', field('default', $._attr_expr))),
    )),

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
      field('type', $.return_type),
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
      // String/symbol pattern atoms (unified into type system)
      $.pattern_char_class,     // \d, \w, \s, \a, \. (any character)
    ),

    // Occurrence applied to primary type: T?, T+, T*, T[n]
    // No chaining allowed (like regex) - use explicit grouping: (T*)[2]
    // Use prec.dynamic(1) to prefer occurrence over concat_type continuation.
    occurrence_type: $ => prec.dynamic(1, prec.right(seq(
      field('operand', $.primary_type),
      field('operator', $.occurrence),
    ))),

    // Prefix negation: !T (for string/symbol patterns: !\d)
    // Validated in AST builder for context-appropriate usage.
    negation_type: $ => prec.right(seq(
      '!', field('operand', $.primary_type),
    )),

    // Constrained type: base_type that (constraint_expr)
    // e.g. int that (5 < ~ < 10), string that (len(~) > 0)
    // The constraint uses ~ to refer to the value being checked
    // Parentheses required to avoid grammar ambiguity with index expressions
    constrained_type: $ => seq(
      field('base', $.unary_type),
      'that',
      '(',
      field('constraint', $._expr),
      ')',
    ), 

    // Unary type: primary type with optional occurrence modifier
    // Replaces the old unary_type → _quantified_type chain
    unary_type: $ => choice(
      $.occurrence_type,
      $.negation_type,          // !T - prefix negation
      $.primary_type,
      $.constrained_type
    ),

    binary_type: $ => choice(
      ...type_pattern(choice($._type_expr)),
    ),
    
    // Unified type expression - flattened hierarchy
    // Structural precedence (tightest to loosest):
    //   primary_type > occurrence_type > constrained/concat > binary > fn_type
    _type_expr: $ => choice(
      $.unary_type,            // covers primary_type and occurrence_type
      // $.concat_type,        // whitespace-separated type terms (patterns)
      $.binary_type,           // alternation: T | U, T & U, T ! U
      $.fn_type,
    ),

    grouped_type: $ => seq(
      '(', $._string_type_expr, ')',
    ),

    // Type concatenation (for string/symbol patterns): whitespace-separated sequence of type terms.
    // e.g. \d[3] "-" \d[3] "-" \d[4]
    // Only valid inside string/symbol pattern definitions; AST builder rejects elsewhere.
    // Terms are unary_type (primary_type possibly with occurrence).
    concat_type: $ => prec.left(seq(
      choice($.unary_type, $.grouped_type),
      repeat1(choice($.unary_type, $.grouped_type)),
    )),

    string_binary_type: $ => choice(
      ...type_pattern(choice($._string_type_expr)),
    ),

    _string_type_expr: $ => choice(
      $.unary_type,            // covers primary_type and occurrence_type
      $.concat_type,           // whitespace-separated type terms (patterns)
      alias($.string_binary_type, $.binary_type),   
      $.grouped_type        // alternation: T | U, T & U, T ! U
    ),

    return_occurrence_type: $ => seq(choice($.base_type, $.identifier), optional($.occurrence)),

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
      choice($._type_expr, $._string_type_expr))),

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

    // Object-level constraint: that (expr)
    that_constraint: $ => seq('that', '(', field('constraint', $._expr), ')'),

    // type_stam handles type aliases, string patterns, and symbol patterns.
    // The leading keyword distinguishes them; AST builder checks the text.
    type_stam: $ => seq(
      optional(field('pub', 'pub')),
      field('kind', choice('type', 'string', 'symbol')),
      field('declare', alias($.type_assign, $.assign_expr)),
      repeat(seq(',', field('declare', alias($.type_assign, $.assign_expr))))
    ),

    // top-level type definitions: type_stam | object_type

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
      '\\.',  // any character
    )),

    // NOTE: string_pattern and symbol_pattern are now handled by type_stam.
    // type_stam's 'kind' field distinguishes 'type' vs 'string' vs 'symbol'.

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
});

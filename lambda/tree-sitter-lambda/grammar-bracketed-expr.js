/**
 * @file Lambda Script grammar for tree-sitter
 * @author Henry Luo
 * @license MIT
 */

// @ts-check
/// <reference types="../tree-sitter-dsl.d.ts" />

// Creates a rule to match one or more of the rules separated by a comma
function comma_sep1(rule) {
  return seq(rule, repeat(seq(',', rule)));
}

// Creates a rule to optionally match one or more of the rules separated by a comma
function comma_sep(rule) {
  return optional(comma_sep1(rule));
}

const digit = /\d/;
const linebreak = /\r\n|\n/;
const decimal_digits = /\d+/;
const integer_literal = seq(choice('0', seq(/[1-9]/, optional(decimal_digits))));
const signed_integer_literal = seq(optional('-'), integer_literal);
const base64_unit = /[A-Za-z0-9+/]{4}/;
const base64_padding = choice(/[A-Za-z0-9+/]{2}==/, /[A-Za-z0-9+/]{3}=/);

function binary_expr($, exclude_relation) { 
  return [
    ['and', 'logical_and'],
    ['or', 'logical_or'],
    ['+', 'binary_plus'],
    ['-', 'binary_plus'],
    ['*', 'binary_times'],
    ['/', 'binary_times'],
    ['div', 'binary_times'],
    ['%', 'binary_times'],
    ['**', 'binary_pow', 'right'],
    ['==', 'binary_eq'],
    ['!=', 'binary_eq'],        
    ...(exclude_relation ? []:
    [['<', 'binary_relation'],
    ['<=', 'binary_relation'],
    ['>=', 'binary_relation'],
    ['>', 'binary_relation']]),
    ['to', 'binary_relation'],
    ['in', 'binary_relation'],
  ].map(([operator, precedence, associativity]) =>
    (associativity === 'right' ? prec.right : prec.left)(precedence, seq(
      field('left', $._expression), // operator === 'in' ? choice($._expression, $.private_property_identifier) : $._expression),
      field('operator', operator),
      field('right', $._expression),
    )),
  );
}

function time() { 
  // time: hh:mm:ss.sss or hh:mm:ss or hh:mm or hh.hhh or hh:mm.mmm
  return seq(digit, digit, optional(seq(':', digit, digit)), optional(seq(':', digit, digit)), 
    optional((seq('.', digit, digit, digit))),
    // timezone
    optional(choice('z', 'Z', seq(choice('+', '-'), digit, digit, optional(seq(':', digit, digit)))))
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
    $._expression,
    $._statement,
  ],

  inline: $ => [
    $._literal,
    $._parenthesized_expr,
    $._arguments,
    $._content_item,
    $._statement,
    $._content,
    $._content_expr,
    $._number,
    $._datetime,
    $._list_expr,
  ],

  precedences: $ => [[
    $.attr,
    $.call_expr,
    $.primary_expr,
    $.unary_expr,
    'binary_pow',
    'binary_times',
    'binary_plus',
    'binary_shift',
    'binary_compare',
    'binary_relation',
    'binary_eq',
    'bitwise_and',
    'bitwise_xor',
    'bitwise_or',
    'logical_and',
    'logical_or',
    'to',
    'in',
    $.let_expr,
    $.if_expr,
    $.for_expr,
    $.assign_expr
  ]],

  conflicts: $ => [
    [$.binary_expr, $.binary_no_relation_expr],
  ],

  rules: {
    // $._literal at top-level for JSON and Mark compatibility
    document: $ => seq(repeat($._statement), optional($.content)),

    _statement: $ => choice($.fn_definition),

    _content_item: $ => choice(
      $._list_expr,
      $.if_stam, 
      $.for_stam,
      $.map, $.element,
      $.string,
      $.symbol,
      $._number,
      $._datetime,
      $.binary,
      $.true,
      $.false,
    ),

    _content: $ => repeat1(choice(
      $._content_item,
      // we require one item to follow 'let' statement
      seq($.let_stam, choice(linebreak, ';'), $._content)
    )),

    content: $ => $._content,

    _list_expr: $ => seq('(', $._expression, repeat(seq(',', $._expression)), ')'),

    _literal: $ => choice(
      $.lit_map,
      $.lit_array,
      $.lit_element,
      $._number,
      $.string,
      $.symbol,
      $._datetime,
      $.binary,
      $.true,
      $.false,
      $.null,
    ),

    lit_map: $ => prec(101, seq(
      '{', comma_sep(seq(
        field('name', choice($.string, $.symbol, $.identifier)),
        ':',
        field('as', $._literal),
      )), '}',
    )),

    pair: $ => seq(
      field('name', choice($.string, $.symbol, $.identifier)),
      ':',
      field('as', $._expression),
    ),
    map: $ => seq(
      '{', comma_sep($.pair), '}',
    ),    

    lit_array: $ => prec(101, seq(
      '[', comma_sep($._literal), ']',
    )),

    array: $ => seq(
      '[', comma_sep($._expression), ']',
    ),    

    range: $ => seq(
      $._expression, 'to', $._expression,
    ),

    lit_attr: $ => prec(101, seq(
      field('name', choice($.string, $.symbol, $.identifier)),
      ':',
      field('as', $._literal),
    )),
    
    attr: $ => prec(100, seq(
      field('name', choice($.string, $.symbol, $.identifier)),
      ':',
      field('as', $._content_expr),
    )),
    attrs: $ => prec.left(seq($.attr, repeat(seq(',', $.attr)))),

    element: $ => seq('<', $.identifier,
      choice(
        seq($.attrs, optional(seq(',', $._content))),
        optional($._content)
      ),
    '>'),

    lit_element: $ => prec(100, seq('<', $.identifier,
      repeat($.lit_attr), repeat(prec(101,$._literal)),
    '>')),

    // no empty strings under Mark/Lambda
    string: $ => seq('"', $._string_content, '"'),

    _string_content: $ => repeat1(choice(
      $.string_content,
      $.escape_sequence,
    )),

    // no empty string, and string can span multiple lines
    string_content: _ => token.immediate(/[^\\"]+/),

    // no empty symbol under Mark/Lambda
    symbol: $ => seq("'", $._symbol_content, "'"),

    _symbol_content: $ => repeat1(choice(
      $.symbol_content,
      $.escape_sequence,
    )),

    symbol_content: _ => token.immediate(/[^\\'\n]+/),    

    escape_sequence: _ => token.immediate(seq(
      '\\',
      /(\"|\\|\/|b|f|n|r|t|u)/,
    )),

    binary: $ => seq("b'", /\s*/, choice($.hex_binary, $.base64_binary), /\s*/, "'"),

    // whitespace allowed in hex and base64 binary
    hex_binary: _ => token(seq(optional("\\x"), repeat1(/[0-9a-fA-F\s]/))),

    base64_binary: _ => token(seq("\\64", 
      repeat1(choice(base64_unit, /\s+/)), optional(base64_padding)
    )),

    _number: $ => choice($.integer, $.float),

    float: _ => {
      const signed_integer = seq(optional('-'), decimal_digits);
      const exponent_part = seq(choice('e', 'E'), signed_integer);
      const decimal_literal = choice(
        seq(signed_integer_literal, '.', optional(decimal_digits), optional(exponent_part)),
        seq(signed_integer_literal, exponent_part),
      );
      return token(decimal_literal);
    },

    integer: $ => {
      return token(signed_integer_literal);
    },

    // time: hh:mm:ss.sss or hh:mm:ss or hh:mm or hh.hhh or hh:mm.mmm
    time: _ => token.immediate(time()),
    // date-time
    datetime: _ => token.immediate(
      seq(optional('-'), digit, digit, digit, digit, optional(seq('-', digit, digit)), optional(seq('-', digit, digit)),
        optional(seq(/\s+/, time()))
      )),

    _datetime: $ => seq("t'", /\s*/, choice($.datetime, $.time), /\s*/, "'"),

    index: $ => {
      return token(integer_literal);
    },    

    true: _ => 'true',
    false: _ => 'false',

    null: _ => 'null',

    comment: _ => token(choice(
      seq('//', /[^\r\n\u2028\u2029]*/),
      seq(
        '/*',
        /[^*]*\*+([^/*][^*]*\*+)*/,
        '/',
      ),
    )),

    // Expressions
    _parenthesized_expr: $ => seq(
      '(', $._expression, ')',
    ),

    _expression: $ => choice(
      $.primary_expr,
      // $.await_expression,
      $.unary_expr,
      $.binary_expr,
      $.let_expr,
      $.if_expr,
      $.for_expr,
    ),

    // exclude binary comparison exprs
    _content_expr: $ => prec(100, choice(
      $.primary_expr,
      $.unary_expr,
      alias($.binary_no_relation_expr, $.binary_expr),
      $.let_expr,
      $.if_expr,
      $.for_expr,
    )),

    primary_expr: $ => choice(
      $.subscript_expr,
      $.member_expr,
      $._parenthesized_expr,
      $.identifier,
      $.null,
      $.true,
      $.false,
      $._number,
      $._datetime,
      $.string,
      $.symbol,
      $.binary,
      $.array,
      $.map,
      $.lit_element,
      $.element,
      // $.function_expression,
      // $.arrow_function,
      // $.class,
      // $.meta_property,
      $.call_expr,
    ),

    import: _ => token('import'),

    spread_element: $ => seq('...', $._expression),

    _arguments: $ => seq(
      '(', comma_sep(optional(
      field('argument', choice($._expression, $.spread_element)))), ')',
    ),    

    call_expr: $ => seq(
      field('function', choice($._expression, $.import)),
      $._arguments,
    ),

    subscript_expr: $ => seq(
      field('object', $.primary_expr),
      // optional(field('optional_chain', $.optional_chain)),
      '[', field('field', $._expression), ']',
    ),
    
    member_expr: $ => seq(
      field('object',$.primary_expr), ".", 
      field('field', choice($.identifier, $.index))
    ),

    binary_expr: $ => choice(
      ...binary_expr($),
    ),

    binary_no_relation_expr: $ => prec(100, choice(
      ...binary_expr($, true),
    )),

    unary_expr: $ => seq(
      field('operator', choice('not', '-', '+')),
      field('expr', $._expression),
    ),

    identifier: _ => {
      // copied from JS grammar
      const alpha = /[^\x00-\x1F\s\p{Zs}0-9:;`"'@#.,|^&<=>+\-*/\\%?!~()\[\]{}\uFEFF\u2060\u200B\u2028\u2029]|\\u[0-9a-fA-F]{4}|\\u\{[0-9a-fA-F]+\}/;
      const alphanumeric = /[^\x00-\x1F\s\p{Zs}:;`"'@#.,|^&<=>+\-*/\\%?!~()\[\]{}\uFEFF\u2060\u200B\u2028\u2029]|\\u[0-9a-fA-F]{4}|\\u\{[0-9a-fA-F]+\}/;
      return token(seq(alpha, repeat(alphanumeric)));
    },  

    // JS Fn Parameter : Identifier | ObjectBinding | ArrayBinding, Initializer_opt
    parameter: $ => seq(
      field('name', $.identifier), optional(seq(':', field('type', $.type_annotation))),
    ),

    fn_definition: $ => seq(
      'fn', field('name', $.identifier), 
      '(', field('declare', $.parameter), repeat(seq(',', field('declare', $.parameter))), ')', 
      '{', field('body', $._expression), '}',
    ),

    type_annotation: $ => choice(
      'null',
      'any',
      'error',
      'boolean',
      'int',  // int64
      'float',  // float64
      'decimal',  // big decimal
      'number',
      'string',
      'symbol',
      'date',
      'time',
      'datetime',
      'list',
      'array',
      'map',
      'element',
      // 'object',
      'type',
      'function',
    ),

    assign_expr: $ => seq(
      field('name', $.identifier), 
      optional(seq(':', field('type', $.type_annotation))), '=', field('as', $._expression),
    ),

    let_expr: $ => seq(
      'let', '(', field('declare', $.assign_expr), repeat(seq(',', field('declare', $.assign_expr))), ')', 
      field('then', $._expression),
    ),
    
    let_stam: $ => seq('let', 
      field('declare', $.assign_expr), repeat(seq(',', field('declare', $.assign_expr)))
    ),

    if_expr: $ => prec.right(seq(
      'if', '(', field('cond', $._expression), ')',
      field('then', $._expression),
      optional(seq('else', field('else', $._expression))),
    )),

    if_stam: $ => prec.right(seq(
      'if', field('cond', $._expression),
      '{', field('then', $.content), '}',
      optional(seq('else', '{', field('else', $.content), '}')),
    )),

    loop_expr: $ => seq(
      field('name', $.identifier), 'in', field('as', $._expression),
    ),
    
    for_expr: $ => seq(
      'for', '(', field('declare', $.loop_expr), 
      repeat(seq(',', field('declare', $.loop_expr))), ')', 
      field('then', $._expression),
    ),  

    for_stam: $ => seq(
      'for', field('declare', $.loop_expr), 
      repeat(seq(',', field('declare', $.loop_expr))), 
      '{', field('then', $.content), '}'
    ),     

  },
});



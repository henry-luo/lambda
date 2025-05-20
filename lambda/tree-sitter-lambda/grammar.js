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

function binary_expr($) { 
  return [
    ['and', 'logical_and'],
    ['or', 'logical_or'],
    ['+', 'binary_plus'],
    ['-', 'binary_plus'],
    ['*', 'binary_times'],
    ['/', 'binary_times'],
    ['_/', 'binary_times'],
    ['%', 'binary_times'],
    ['**', 'binary_pow', 'right'],
    ['==', 'binary_eq'],
    ['!=', 'binary_eq'],        
    ['<', 'binary_relation'],
    ['<=', 'binary_relation'],
    ['>=', 'binary_relation'],
    ['>', 'binary_relation'],
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

function pattern_expr($) { 
  return [
    ['|', 'union'],
    ['&', 'intersect'],
    ['!', 'exclude'],  // set1 ! set2, elements in set1 but not in set2.
    ['^', 'exclude'],  // set1 ^ set2, elements in either set, but not both.
  ].map(([operator, precedence, associativity]) =>
    (associativity === 'right' ? prec.right : prec.left)(precedence, seq(
      field('left', $.primary_type), // operator === 'in' ? choice($._expression, $.private_property_identifier) : $._expression),
      field('operator', operator),
      field('right', $.primary_type),
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
  ],

  inline: $ => [
    $._literal,
    $._parenthesized_expr,
    $._arguments,
    $._content_item,
    $._content_expr,
    $._number,
    $._datetime,
    $._unsigned_number,
  ],

  precedences: $ => [[
    $.attr,
    $.fn_expr_stam,
    $.fn_expr,
    $.primary_expr,
    $.primary_type,
    $.unary_expr,
    'intersect',
    'exclude',
    'union',
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
    $.assign_expr,
    $.if_expr,
    $.for_expr,     
  ]],

  conflicts: $ => [
    [$.null, $.built_in_type],
    [$.content, $.binary_expr],
    [$.primary_expr, $.parameter]
  ],

  rules: {
    // $._literal at top-level for JSON and Mark compatibility
    document: $ => optional($.content),

    _content_item: $ => choice(
      $.if_stam, 
      $.for_stam,
      $.fn_stam,
    ),

    _content_expr: $ => choice(
      $._expression, 
      $.let_stam,
      $.fn_expr_stam,
    ),    

    content: $ => seq(
      repeat(
        choice( 
          seq($._content_expr, choice(linebreak, ';')), 
          $._content_item
        )
      ), 
      // for last expr, ';' is optional
      choice(
        seq($._content_expr, optional(choice(linebreak, ';'))), 
        $._content_item
      )
    ),

    list: $ => seq('(', 
      choice($._expression, $.assign_expr), 
      repeat1(seq(',', choice($._expression, $.assign_expr))), ')'
    ),

    _literal: $ => choice(
      $._number,
      $.string,
      $.symbol,
      $._datetime,
      $.binary,
      $.true,
      $.false,
      $.null,
    ),

    pair: $ => seq(
      field('name', choice($.string, $.symbol, $.identifier)),
      ':',
      field('as', $._expression),
    ),
    map: $ => seq(
      '{', comma_sep($.pair), '}',
    ),

    array: $ => seq(
      '[', comma_sep($._expression), ']',
    ),    

    range: $ => seq(
      $._expression, 'to', $._expression,
    ),
    
    attr: $ => prec(100, seq(
      field('name', choice($.string, $.symbol, $.identifier)),
      ':',
      field('as', $._expression),
    )),
    attrs: $ => prec.left(seq($.attr, repeat(seq(',', $.attr)))),

    element: $ => seq('<', $.identifier,
      choice(
        seq($.attrs, optional(seq(choice(linebreak, ';'), $.content))),
        optional($.content)
      ),
    '>'),

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

    unsigned_float: _ => {
      const signed_integer = seq(optional('-'), decimal_digits);
      const exponent_part = seq(choice('e', 'E'), signed_integer);
      const decimal_literal = choice(
        seq(integer_literal, '.', optional(decimal_digits), optional(exponent_part)),
        seq(integer_literal, exponent_part),
      );
      return token(decimal_literal);
    },    

    _unsigned_number: $ => choice(alias(integer_literal, $.integer), alias($.unsigned_float, $.float)),

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

    null: _ => 'null',
    true: _ => 'true',
    false: _ => 'false',
    inf: _ => 'inf',
    nan: _ => 'nan',

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

    _expression: $ => prec.left(choice(
      $.primary_expr,
      // $.await_expression,
      $.unary_expr,
      $.binary_expr,
      $.if_expr,
      $.for_expr,
      $.fn_expr,
    )),

    primary_expr: $ => choice(
      $.subscript_expr,
      $.member_expr,
      $._parenthesized_expr,
      $.identifier,
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
      field('function', choice($.primary_expr, $.import)),
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

    unary_expr: $ => prec.left(seq(
      field('operator', choice('not', '-', '+')),
      field('operand', $._expression),
    )),

    identifier: _ => {
      // copied from JS grammar
      const alpha = /[a-zA-Z_]/;
      const alphanumeric = /[^\x00-\x1F\s\p{Zs}:;`"'@#.,|^&<=>+\-*/\\%?!~()\[\]{}\uFEFF\u2060\u200B\u2028\u2029]|\\u[0-9a-fA-F]{4}|\\u\{[0-9a-fA-F]+\}/;
      return token(seq(alpha, repeat(alphanumeric)));
    },  

    // JS Fn Parameter : Identifier | ObjectBinding | ArrayBinding, Initializer_opt
    parameter: $ => seq(
      field('name', $.identifier), optional(seq(':', field('type', $.type_annotation))),
    ),

    fn_stam: $ => seq(
      'fn', field('name', $.identifier), 
      '(', field('declare', $.parameter), repeat(seq(',', field('declare', $.parameter))), ')', 
      // return type
      optional(seq(':', field('type', $.type_annotation))),      
      '{', field('body', $.content), '}',
    ),

    fn_expr_stam: $ => seq('fn', field('name', $.identifier), 
      '(', field('declare', $.parameter), repeat(seq(',', field('declare', $.parameter))), ')', 
      // return type
      optional(seq(':', field('type', $.type_annotation))),      
      '=>', field('body', $._expression)
    ),

    // anonymous function
    fn_expr: $ => choice(
      seq('fn', 
        '(', field('declare', $.parameter), repeat(seq(',', field('declare', $.parameter))), ')', 
        // return type
        optional(seq(':', field('type', $.type_annotation))),      
        '{', field('body', $.content), '}'
      ),
      seq(
        '(', field('declare', $.parameter), repeat(seq(',', field('declare', $.parameter))), ')', 
        // return type
        optional(seq(':', field('type', $.type_annotation))),      
        '=>', field('body', $._expression)
      ),      
    ),

    built_in_type: $ => choice(
      'null',
      'any',
      'error',
      'boolean',
      // 'int8', 'int16', 'int32', 'int64',
      'int',  // int64
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
      'object',
      'type',
      'function',
    ),

    occurrence: $ => choice('?', '+', '*'),

    map_item_type: $=> seq(
      field('key', choice($.identifier, $.symbol)), ':', field('value', $.type_annotation)
    ),

    map_type: $ => seq('{', 
      $.map_item_type, repeat(seq(choice(linebreak, ';'), $.map_item_type)), '}'
    ),

    primary_type: $ => prec.left(seq(choice(
      $.built_in_type,
      $.identifier,
      $._literal,
      // array type
      $.map_type,
      // entity type
      // fn type
    ), optional($.occurrence))),

    binary_type: $ => choice(
      ...pattern_expr($),
    ),

    type_annotation: $ => choice(
      $.primary_type,
      $.binary_type
    ),

    type_definition: $ => seq(
      'type', field('name', $.identifier), '=', $.type_annotation,
    ),    

    assign_expr: $ => seq(
      field('name', $.identifier), 
      optional(seq(':', field('type', $.type_annotation))), '=', field('as', $._expression),
    ),
    
    let_stam: $ => seq('let', 
      field('declare', $.assign_expr), repeat(seq(',', field('declare', $.assign_expr)))
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

    loop_expr: $ => seq(
      field('name', $.identifier), 'in', field('as', $._expression),
    ),
    
    for_expr: $ => seq(
      'for', '(', field('declare', $.loop_expr), 
      repeat(seq(',', field('declare', $.loop_expr))), ')', 
      field('then', $._expression),
    ),  

    for_stam: $ => seq(
      'for', seq(field('declare', $.loop_expr), repeat(seq(',', field('declare', $.loop_expr)))),
      '{', field('then', $.content), '}'
    ),     

  },
});



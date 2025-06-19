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
const decimal_digits = /\d+/;
const integer_literal = seq(choice('0', seq(/[1-9]/, optional(decimal_digits))));
const signed_integer_literal = seq(optional('-'), integer_literal);
const signed_integer = seq(optional('-'), decimal_digits);
const exponent_part = seq(choice('e', 'E'), signed_integer);
const float_literal = choice(
  seq(signed_integer_literal, '.', optional(decimal_digits), optional(exponent_part)),
  seq(optional('-'), '.', decimal_digits, optional(exponent_part)),
  seq(signed_integer_literal, exponent_part),
);
const decimal_literal = choice(
  seq(signed_integer_literal, '.', optional(decimal_digits)),
  seq(optional('-'), '.', decimal_digits),
);

const base64_unit = /[A-Za-z0-9+/]{4}/;
const base64_padding = choice(/[A-Za-z0-9+/]{2}==/, /[A-Za-z0-9+/]{3}=/);

// need to exclude relational exprs in attr
function binary_expr($, in_attr) {
  let operand = in_attr ? choice($.primary_expr, $.unary_expr, alias($.attr_binary_expr, $.binary_expr)) : $._expression;
  return [
    ['+', 'binary_plus'],
    ['-', 'binary_plus'],
    ['*', 'binary_times'],
    ['/', 'binary_times'],
    ['_/', 'binary_times'],
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
    ['|', 'set_union'],
    ['&', 'set_intersect'],
    ['!', 'set_exclude'],  // set1 ! set2, elements in set1 but not in set2.
    // ['^', 'set_exclude'],  // set1 ^ set2, elements in either set, but not both.    
    ['is', 'set_is_in'],
    ['in', 'set_is_in'],
  ].map(([operator, precedence, associativity]) =>
    (associativity === 'right' ? prec.right : prec.left)(precedence, seq(
      field('left', operand),
      field('operator', operator),
      field('right', operand),
    )),
  );
}

function type_pattern(primary_type) { 
  return [
    ['|', 'set_union'],
    ['&', 'set_intersect'],
    ['!', 'set_exclude'],  // set1 ! set2, elements in set1 but not in set2.
    ['^', 'set_exclude'],  // set1 ^ set2, elements in either set, but not both.
  ].map(([operator, precedence, associativity]) =>
    (associativity === 'right' ? prec.right : prec.left)(precedence, seq(
      field('left', primary_type), // operator === 'in' ? choice($._expression, $.private_property_identifier) : $._expression),
      field('operator', operator),
      field('right', primary_type),
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

function built_in_types(include_null) { 
  let types = [
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
  ];
  return include_null ? choice('null', ...types) : choice(...types);
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
    $._content_item,
    $._content_expr,
    $._number,
    $._datetime,
    $._unsigned_number,
    $._expression,
    $._attr_expr,
    $._import_stam,
  ],

  precedences: $ => [[
    $.fn_expr_stam,
    $.sys_func,
    $.primary_expr,
    $.primary_type,
    $.unary_expr,
    // binary operators
    'binary_pow',
    'binary_times',
    'binary_plus',
    'binary_shift',
    'binary_compare',
    'binary_relation',
    'binary_eq',
    'logical_and',
    'logical_or',
    // set operators
    'range_to',
    'set_intersect',
    'set_exclude',
    'set_union',
    'set_is_in',
    $.assign_expr,
    $.if_expr,
    $.for_expr,
    $.fn_expr
  ]],

  conflicts: $ => [
    // [$.content, $.binary_expr],
    // [$.primary_expr, $.parameter],
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

    _content_item: $ => choice(
      $.if_stam, 
      $.for_stam,
      $.fn_stam,
      $.string,
      $.map,
      $.element,
    ),

    _content_expr: $ => choice(
      $._attr_expr, 
      $.let_stam,
      $.pub_stam,
      $.fn_expr_stam,
      $.type_definition,
    ),    

    content: $ => seq(
      repeat(
        choice( 
          seq($._content_expr, choice(linebreak, ';')), 
          $._content_item
        )
      ), 
      // for last content expr, ';' is optional
      choice(
        seq($._content_expr, optional(choice(linebreak, ';'))), 
        $._content_item
      )
    ),

    list: $ => seq('(', 
      choice($._expression, $.assign_expr), 
      repeat1(seq(',', choice($._expression, $.assign_expr))), ')'
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

    pair: $ => seq(
      field('name', choice($.string, $.symbol, $.identifier)),
      ':',
      field('as', $._expression),
    ),

    map: $ => seq(
      '{', comma_sep(choice($.pair, $._expression)), '}',
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

    // expr excluding comparison exprs
    _attr_expr: $ => prec.left(choice(
      $.primary_expr,
      $.unary_expr,
      alias($.attr_binary_expr, $.binary_expr),
      $.if_expr,
      $.for_expr,
      $.fn_expr,
    )),

    attr: $ => seq(
      field('name', choice($.string, $.symbol, $.identifier)), 
      ':', field('as', $._attr_expr)
    ),

    element: $ => seq('<', $.identifier,
      choice(
        seq(choice($.attr, seq('&', $._attr_expr)), 
          repeat(seq(',', choice($.attr, seq('&', $._attr_expr)))), 
          optional(seq(choice(linebreak, ';'), $.content))
        ),
        optional($.content)
      ),'>'
    ),

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

    _number: $ => choice($.integer, $.float, $.decimal),

    integer: $ => {
      return token(signed_integer_literal);
    },

    float: _ => {
      return token(float_literal);
    },

    decimal: $ => {
      // no e-notation for decimal
      return token( seq(choice(decimal_literal, signed_integer_literal), choice('n','N')) );
    },

    unsigned_float: _ => {
      const signed_integer = seq(optional('-'), decimal_digits);
      const exponent_part = seq(choice('e', 'E'), signed_integer);
      const float_literal = choice(
        seq(integer_literal, '.', optional(decimal_digits), optional(exponent_part)),
        seq(integer_literal, exponent_part),
      );
      return token(float_literal);
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

    // Expressions
    _parenthesized_expr: $ => seq(
      '(', $._expression, ')',
    ),

    _expression: $ => prec.left(choice(
      $.primary_expr,
      $.unary_expr,
      $.binary_expr,
      $.if_expr,
      $.for_expr,
      $.fn_expr,
    )),

    // prec(50) to make primary_expr higher priority than content
    primary_expr: $ => prec(50,choice(
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
      $.subscript_expr,
      $.member_expr,
      $.call_expr,
      $.sys_func,
      $._parenthesized_expr,
    )),

    import: _ => token('import'),

    spread_argument: $ => seq('...', $._expression),

    _arguments: $ => seq(
      '(', comma_sep(optional(
      field('argument', choice($._expression, $.spread_argument)))), ')',
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
      ...binary_expr($, false),
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
      optional(field('pub', 'pub')), // note: pub fn is only allowed at global level
      'fn', field('name', $.identifier), 
      '(', field('declare', $.parameter), repeat(seq(',', field('declare', $.parameter))), ')', 
      // return type
      optional(seq(':', field('type', $.type_annotation))),      
      '{', field('body', $.content), '}',
    ),

    fn_expr_stam: $ => seq(
      optional(field('pub', 'pub')), // note: pub fn is only allowed at global level
      'fn', field('name', $.identifier), 
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

    // system function call
    // prec(50) to make it higher priority than base types
    sys_func: $ => prec(50, seq(
      field('function', choice(
        'length', 'type', 'int', 'float', 'number', 'string', 'char', 'symbol',
        'datetime', 'date', 'time', 'today', 'justnow',
        'set', 'slice',
        'all', 'any', 'min', 'max', 'sum', 'avg', 'abs', 'round', 'floor', 'ceil',
        'print', 'error',
      )), $._arguments,
    )),

    occurrence: $ => choice('?', '+', '*'),

    map_type_item: $=> seq(
      field('name', choice($.identifier, $.symbol)), ':', field('as', $.type_annotation)
    ),

    map_type: $ => seq('{', 
      $.map_type_item, repeat(seq(choice(linebreak, ','), $.map_type_item)), '}'
    ),

    base_type: $ => built_in_types(true),

    _non_null_base_type: $ => built_in_types(false),

    primary_type: $ => choice(
      $.base_type,
      $.identifier,
      $._non_null_literal, // null is now a base type
      // array type
      $.map_type,
      // entity type
      // fn type
    ),

    type_occurrence: $ => seq($.primary_type, optional($.occurrence)),

    binary_type: $ => choice(
      ...type_pattern($.type_occurrence),
    ),

    type_annotation: $ => choice(
      $.primary_type,
      $.binary_type,
    ),

    type_assign: $ => seq(field('name', $.identifier), '=', field('as', $.type_annotation)),

    type_definition: $ => seq(
      'type', field('declare', alias($.type_assign, $.assign_expr)),
    ),

    assign_expr: $ => seq(
      field('name', $.identifier), 
      optional(seq(':', field('type', $.type_annotation))), '=', field('as', $._expression),
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



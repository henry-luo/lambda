/**
 * @file Lambda Script grammar for tree-sitter
 * @author Henry Luo
 * @license MIT
 */

// @ts-check
/// <reference types="../tree-sitter-dsl.d.ts" />

// Creates a rule to match one or more of the rules separated by a comma
function commaSep1(rule) {
  return seq(rule, repeat(seq(',', rule)));
}

// Creates a rule to optionally match one or more of the rules separated by a comma
function commaSep(rule) {
  return optional(commaSep1(rule));
}

const decimalDigits = /\d+/;

module.exports = grammar({
  name: "lambda",

  extras: $ => [
    /\s/,
    $.comment,
  ],

  word: $ => $.identifier,

  // an array of hidden rule names for the generated node types
  supertypes: $ => [
    $._expression,
    $._statement,
  ],

  inline: $ => [
    $._literal,
    $._parenthesized_expr,
  ],

  precedences: $ => [[
    $._literal,
    $.primary_expr,
    $.unary_expr,
    'binary_exp',
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
    $.for_expr
  ]],

  // conflicts: $ => [],

  rules: {
    // $._literal at top-level for JSON and Mark compatibility
    document: $ => repeat($._statement),

    _statement: $ => choice($._literal, $.expr_stam, $.fn_definition, $.let_stam),

    expr_stam: $ => seq('{', $._expression, '}'),

    _literal: $ => choice(
      $.lit_map,
      $.lit_array,
      $.number,
      $.string,
      $.symbol,      
      $.true,
      $.false,
      $.null,
    ),

    lit_map: $ => seq(
      '{', commaSep(seq(
        field('name', choice($.string, $.symbol, $.identifier)),
        ':',
        field('then', $._literal),
      )), '}',
    ),

    pair: $ => seq(
      field('name', choice($.string, $.symbol, $.identifier)),
      ':',
      field('then', $._expression),
    ),
    map: $ => seq(
      '{', commaSep($.pair), '}',
    ),    

    lit_array: $ => seq(
      '[', commaSep(optional($._literal)), ']',
    ),

    array: $ => seq(
      '[', commaSep(optional($._expression)), ']',
    ),    

    range: $ => seq(
      $.number, 'to', $.number,
    ),

    // no empty strings under Mark/Lambda
    string: $ => seq('"', $._string_content, '"'),

    _string_content: $ => repeat1(choice(
      $.string_content,
      $.escape_sequence,
    )),

    // no empty string, and string can span multiple lines
    string_content: _ => token.immediate(prec(1, /[^\\"]+/)),

    // no empty symbol under Mark/Lambda
    symbol: $ => seq("'", $._symbol_content, "'"),

    _symbol_content: $ => repeat1(choice(
      $.symbol_content,
      $.escape_sequence,
    )),

    symbol_content: _ => token.immediate(prec(1, /[^\\'\n]+/)),    

    escape_sequence: _ => token.immediate(seq(
      '\\',
      /(\"|\\|\/|b|f|n|r|t|u)/,
    )),

    number: _ => {
      const signedInteger = seq(optional('-'), decimalDigits);
      const exponentPart = seq(choice('e', 'E'), signedInteger);
      const decimalIntegerLiteral = seq(
        optional('-'),
        choice('0', seq(/[1-9]/, optional(decimalDigits))),
      );
      const decimalLiteral = choice(
        seq(decimalIntegerLiteral, '.', optional(decimalDigits), optional(exponentPart)),
        seq(decimalIntegerLiteral, optional(exponentPart)),
      );
      return token(decimalLiteral);
    },

    integer: _ => {
      const integerLiteral = seq(
        choice('0', seq(/[1-9]/, optional(decimalDigits))),
      );
      return token(integerLiteral);
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

    primary_expr: $ => choice(
      $.subscript_expr,
      $.member_expr,
      $._parenthesized_expr,
      $.identifier,
      // alias($._reserved_identifier, $.identifier),
      // $.this,
      // $.super,
      $.number,
      $.string,
      $.symbol,
      // $.template_string,
      // $.regex,
      $.true,
      $.false,
      $.null,
      $.array,
      $.map,
      // $.function_expression,
      // $.arrow_function,
      // $.generator_function,
      // $.class,
      // $.meta_property,
      // $.call_expression,
    ),

    subscript_expr: $ => seq(
      field('object', $.primary_expr),
      // optional(field('optional_chain', $.optional_chain)),
      '[', field('field', $._expression), ']',
    ),
    
    member_expr: $ => seq(
      field('object',$.primary_expr), ".", 
      field('field', choice($.identifier, $.integer))
    ),

    binary_expr: $ => choice(
      ...[
        ['and', 'logical_and'],
        ['or', 'logical_or'],
        ['+', 'binary_plus'],
        ['-', 'binary_plus'],
        ['*', 'binary_times'],
        ['/', 'binary_times'],
        ['%', 'binary_times'],
        ['**', 'binary_exp', 'right'],
        ['<', 'binary_relation'],
        ['<=', 'binary_relation'],
        ['==', 'binary_eq'],
        ['!=', 'binary_eq'],
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
      ),
    ),

    unary_expr: $ => seq(
      field('operator', choice('not', '-', '+')),
      field('then', $._expression),
    ),

    identifier: _ => {
      // copied from JS grammar
      const alpha = /[^\x00-\x1F\s\p{Zs}0-9:;`"'@#.,|^&<=>+\-*/\\%?!~()\[\]{}\uFEFF\u2060\u200B\u2028\u2029]|\\u[0-9a-fA-F]{4}|\\u\{[0-9a-fA-F]+\}/;
      const alphanumeric = /[^\x00-\x1F\s\p{Zs}:;`"'@#.,|^&<=>+\-*/\\%?!~()\[\]{}\uFEFF\u2060\u200B\u2028\u2029]|\\u[0-9a-fA-F]{4}|\\u\{[0-9a-fA-F]+\}/;
      return token(seq(alpha, repeat(alphanumeric)));
    },  

    fn_definition: $ => seq(
      'fn', 
      field('name', $.identifier), '(', ')', '{', 
      field('body', $._expression), '}',
    ),

    assign_expr: $ => seq(
      field('name', $.identifier), '=', field('then', $._expression),
    ),

    let_expr: $ => seq(
      'let', repeat1(seq(field('declare', $.assign_expr), ',')), 
      field('then', $._expression),
    ),

    if_expr: $ => prec.right(seq(
      'if', '(', field('cond', $._expression), ')',
      field('then', $._expression),
      optional(seq('else', field('else', $._expression))),
    )),

    loop_expr: $ => seq(
      field('name', $.identifier), 'in', field('then', $._expression),
    ),
    
    for_expr: $ => seq(
      'for', '(', field('declare', $.loop_expr), 
      repeat(seq(',', field('declare', $.loop_expr))), ')', 
      field('then', $._expression),
    ),    

    let_stam: $ => seq(
      'let', repeat1(seq(field('declare', $.assign_expr), ',')), ';'
    ),    

  },
});



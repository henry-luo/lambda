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

module.exports = grammar({
  name: "lambda",

  extras: $ => [
    /\s/,
    $.comment,
  ],

  word: $ => $.identifier,

  // an array of hidden rule names
  supertypes: $ => [
    $.expression,
  ],

  inline: $ => [
    $._value,
  ],

  precedences: $ => [
    [
      'member',
      'template_call',
      'call',
      // $.update_expression,
      'unary_void',
      'binary_exp',
      'binary_times',
      'binary_plus',
      'binary_shift',
      'binary_compare',
      'binary_relation',
      'binary_equality',
      'bitwise_and',
      'bitwise_xor',
      'bitwise_or',
      'logical_and',
      'logical_or',
      'ternary',
      // $.sequence_expression,
      $.let_expr,
      $.if_expr,
    ],
  ],  

  // conflicts: $ => [],

  rules: {
    document: $ => repeat(choice($._value, $.fn_definition)),

    _value: $ => choice(
      $.object,
      $.array,
      // $.range,
      $.number,
      $.string,
      $.symbol,      
      $.true,
      $.false,
      $.null,
    ),

    object: $ => seq(
      '{', commaSep($.pair), '}',
    ),

    pair: $ => seq(
      field('key', choice($.string, $.symbol, $.identifier)),
      ':',
      field('value', $._value),
    ),

    array: $ => seq(
      '[',
      commaSep(optional(choice(
        $.expression,
        // $.spread_element,
      ))),
      ']',
    ),    

    range: $ => seq(
      $.number, 'to', $.number,
    ),

    string: $ => seq('"', $._string_content, '"'),
      // seq('"', '"'), - no empty strings under Mark/Lambda

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
      const decimalDigits = /\d+/;
      const signedInteger = seq(optional('-'), decimalDigits);
      const exponentPart = seq(choice('e', 'E'), signedInteger);

      const decimalIntegerLiteral = seq(
        optional('-'),
        choice(
          '0',
          seq(/[1-9]/, optional(decimalDigits)),
        ),
      );

      const decimalLiteral = choice(
        seq(decimalIntegerLiteral, '.', optional(decimalDigits), optional(exponentPart)),
        seq(decimalIntegerLiteral, optional(exponentPart)),
      );

      return token(decimalLiteral);
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
    parenthesized_expression: $ => seq(
      '(', $.expression, ')',
    ),

    expression: $ => choice(
      $.primary_expression,
      // $._jsx_element,
      // $.assignment_expression,
      // $.augmented_assignment_expression,
      // $.await_expression,
      $.unary_expression,
      $.binary_expression,
      // $.ternary_expression,
      // $.update_expression,
      // $.new_expression,
      // $.yield_expression,
      $.let_expr,
      $.if_expr,
    ),

    primary_expression: $ => choice(
      // $.subscript_expression,
      // $.member_expression,
      $.parenthesized_expression,
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
      $.object,
      $.array,
      // $.function_expression,
      // $.arrow_function,
      // $.generator_function,
      // $.class,
      // $.meta_property,
      // $.call_expression,
    ),

    binary_expression: $ => choice(
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
        ['==', 'binary_equality'],
        // ['===', 'binary_equality'],
        ['!=', 'binary_equality'],
        // ['!==', 'binary_equality'],
        ['>=', 'binary_relation'],
        ['>', 'binary_relation'],
        // ['??', 'ternary'],
        // ['instanceof', 'binary_relation'],
        ['to', 'binary_relation'],
        ['in', 'binary_relation'],
      ].map(([operator, precedence, associativity]) =>
        (associativity === 'right' ? prec.right : prec.left)(precedence, seq(
          field('left', $.expression), // operator === 'in' ? choice($.expression, $.private_property_identifier) : $.expression),
          field('operator', operator),
          field('right', $.expression),
        )),
      ),
    ),

    unary_expression: $ => prec.left('unary_void', seq(
      field('operator', choice('not', '-', '+')), // 'typeof', 'void', 'delete', '~' bitwise_not
      field('argument', $.expression),
    )),

    identifier: _ => {
      // copied from JS grammar
      const alpha = /[^\x00-\x1F\s\p{Zs}0-9:;`"'@#.,|^&<=>+\-*/\\%?!~()\[\]{}\uFEFF\u2060\u200B\u2028\u2029]|\\u[0-9a-fA-F]{4}|\\u\{[0-9a-fA-F]+\}/;
      const alphanumeric = /[^\x00-\x1F\s\p{Zs}:;`"'@#.,|^&<=>+\-*/\\%?!~()\[\]{}\uFEFF\u2060\u200B\u2028\u2029]|\\u[0-9a-fA-F]{4}|\\u\{[0-9a-fA-F]+\}/;
      return token(seq(alpha, repeat(alphanumeric)));
    },  

    fn_definition: $ => seq(
      'fn', 
      field('name', $.identifier), '(', ')', '{', 
      field('body', $.expression), '}',
    ),

    let_expr: $ => seq(
      'let', '(', $.identifier, '=', $.expression, ')', $.expression,
    ),

    if_expr: $ => prec.right(seq(
      'if', field('condition', $.parenthesized_expression),
      field('consequence', $.expression),
      optional(field('alternative', seq('else', $.expression))),
    )),

  },
});



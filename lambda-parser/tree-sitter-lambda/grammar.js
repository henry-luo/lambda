/**
 * @file Lambda Script grammar for tree-sitter
 * @author Henry Luo
 * @license MIT
 */

/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

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

  //  an array of hidden rule names
  supertypes: $ => [
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
      // $.arrow_function,
    ],
    // ['assign', $.primary_expression],
    // ['member', 'template_call', 'new', 'call', $.expression],
    // ['declaration', 'literal'],
    // [$.primary_expression, $.statement_block, 'object'],
    // [$.meta_property, $.import],
    // [$.import_statement, $.import],
    // [$.export_statement, $.primary_expression],
    // [$.lexical_declaration, $.primary_expression],
  ],  

  // conflicts: $ => [
  //   [$.primary_expression, $._property_name],
  //   [$.primary_expression, $._property_name, $.arrow_function],
  //   [$.primary_expression, $.arrow_function],
  //   [$.primary_expression, $.method_definition],
  //   [$.primary_expression, $.rest_pattern],
  //   [$.primary_expression, $.pattern],
  //   [$.primary_expression, $._for_header],
  //   [$.variable_declarator, $._for_header],
  //   [$.array, $.array_pattern],
  //   [$.object, $.object_pattern],
  //   [$.assignment_expression, $.pattern],
  //   [$.assignment_expression, $.object_assignment_pattern],
  //   [$.labeled_statement, $._property_name],
  //   [$.computed_property_name, $.array],
  //   [$.binary_expression, $._initializer],
  //   [$.class_static_block, $._property_name],
  // ],

  rules: {
    document: $ => repeat($._value),

    _value: $ => choice(
      $.object,
      $.array,
      $.range,
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

    // string can span multiple lines
    string_content: _ => token.immediate(prec(1, /[^\\"]+/)),

    symbol: $ => seq("'", $._symbol_content, "'"),
    // seq("'", "'"), - no empty symbol under Mark/Lambda

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
      '(',
      $.expression, // $._expressions,
      ')',
    ),

    // _expressions: $ => choice(
    //   $.expression,
    //   $.sequence_expression,
    // ),

    // sequence_expression: $ => prec.right(commaSep1($.expression)),

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
    ),

    primary_expression: $ => choice(
      // $.subscript_expression,
      // $.member_expression,
      $.parenthesized_expression,
      $._identifier,
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

    // member_expression: $ => prec('member', seq(
    //   field('object', choice($.expression, $.primary_expression, $.import)),
    //   choice('.', field('optional_chain', $.optional_chain)),
    //   field('property', choice(
    //     $.private_property_identifier,
    //     alias($.identifier, $.property_identifier))),
    // )),

    // subscript_expression: $ => prec.right('member', seq(
    //   field('object', choice($.expression, $.primary_expression)),
    //   // optional(field('optional_chain', $.optional_chain)),
    //   '[', field('index', $._expressions), ']',
    // )),    

    binary_expression: $ => choice(
      ...[
        ['and', 'logical_and'],
        ['or', 'logical_or'],
        // ['>>', 'binary_shift'],
        // ['>>>', 'binary_shift'],
        // ['<<', 'binary_shift'],
        // ['&', 'bitwise_and'],
        // ['^', 'bitwise_xor'],
        // ['|', 'bitwise_or'],
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

    _identifier: $ => choice(
      // $.undefined,
      $.identifier,
    ),

    identifier: _ => {
      // copied from JS grammar
      const alpha = /[^\x00-\x1F\s\p{Zs}0-9:;`"'@#.,|^&<=>+\-*/\\%?!~()\[\]{}\uFEFF\u2060\u200B\u2028\u2029]|\\u[0-9a-fA-F]{4}|\\u\{[0-9a-fA-F]+\}/;
      const alphanumeric = /[^\x00-\x1F\s\p{Zs}:;`"'@#.,|^&<=>+\-*/\\%?!~()\[\]{}\uFEFF\u2060\u200B\u2028\u2029]|\\u[0-9a-fA-F]{4}|\\u\{[0-9a-fA-F]+\}/;
      return token(seq(alpha, repeat(alphanumeric)));
    },

    _reserved_identifier: _ => choice(
      'get',
      'set',
      'async',
      'static',
      'export',
      'let',
    ),    

    // private_property_identifier: _ => {
    //   const alpha = /[^\x00-\x1F\s\p{Zs}0-9:;`"'@#.,|^&<=>+\-*/\\%?!~()\[\]{}\uFEFF\u2060\u200B\u2028\u2029]|\\u[0-9a-fA-F]{4}|\\u\{[0-9a-fA-F]+\}/;
    //   const alphanumeric = /[^\x00-\x1F\s\p{Zs}:;`"'@#.,|^&<=>+\-*/\\%?!~()\[\]{}\uFEFF\u2060\u200B\u2028\u2029]|\\u[0-9a-fA-F]{4}|\\u\{[0-9a-fA-F]+\}/;
    //   return token(seq('#', alpha, repeat(alphanumeric)));
    // },
  },
});



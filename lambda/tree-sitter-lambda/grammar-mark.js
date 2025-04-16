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
  name: "lambda_script",

  extras: $ => [
    /\s/,
    $.comment,
  ],

  //  an array of hidden rule names
  supertypes: $ => [
    $._value,
  ],

  rules: {
    document: $ => repeat($._value),

    _value: $ => choice(
      $.object,
      $.array,
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
        $._value,
      ))),
      ']',
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

    identifier: _ => {
      // copied from JS grammar
      const alpha = /[^\x00-\x1F\s\p{Zs}0-9:;`"'@#.,|^&<=>+\-*/\\%?!~()\[\]{}\uFEFF\u2060\u200B\u2028\u2029]|\\u[0-9a-fA-F]{4}|\\u\{[0-9a-fA-F]+\}/;
      const alphanumeric = /[^\x00-\x1F\s\p{Zs}:;`"'@#.,|^&<=>+\-*/\\%?!~()\[\]{}\uFEFF\u2060\u200B\u2028\u2029]|\\u[0-9a-fA-F]{4}|\\u\{[0-9a-fA-F]+\}/;
      return token(seq(alpha, repeat(alphanumeric)));
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
  },
});



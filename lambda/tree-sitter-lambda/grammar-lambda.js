/**
 * @file Lambda Script grammar for tree-sitter — OFFICIAL FULL GRAMMAR
 * @author Henry Luo
 * @license MIT
 *
 * This file is the normative surface grammar: it spells out the complete type
 * language, including the type-pattern tiers and the string/symbol pattern
 * islands. Read it to learn what Lambda accepts.
 *
 * It is NOT the grammar the product ships. `grammar.js` is the optimized
 * production grammar: it shares this file's core (via grammar-common.js) and
 * replaces the type layer below with external scanner tokens, which a hand
 * parser reads (see vibe/Lambda_Impl_Type_Scanner.md). The two grammars must
 * accept the same language; `make test-grammar-diff` checks that.
 *
 * Generated artifacts from this file are test-only and live under ./temp/.
 */

// @ts-check
/// <reference types="../tree-sitter-dsl.d.ts" />

const common = require('./grammar-common.js');
const { linebreak, comma_sep, comma_sep1, type_pattern, _attr_content_type } = common;

// ---------------------------------------------------------------------------
// The type-pattern sub-language. This is the half that grammar.js replaces
// with scanner tokens; everything else comes from grammar-common.js.
// ---------------------------------------------------------------------------
const fullTypeLayer = {
    // --- seam ---------------------------------------------------------------
    // The four names grammar-common.js references. In this grammar they expand
    // to the real type rules below; in grammar.js they are scanner tokens.
    _primary_type: $ => $.primary_type,
    _view_atom_type: $ => choice($.element_type, $.identifier, $.base_type),
    _value_island: $ => $.pattern_island,


    // list_type for tuple types and pattern grouping
    // e.g. (int, string) for tuple, ("a" to "z")+ for grouped pattern with occurrence
    // AST builder rejects comma-separated multi-element list_type in pattern context.
    list_type: $ => prec.dynamic(2, seq(
      // list cannot be empty
      '(', seq($._type_pattern, repeat(seq(',', $._type_pattern))), ')',
    )),
    array_type: $ => seq(
      '[', comma_sep($._type_pattern), ']',
    ),
    map_type_item: $=> seq(
      field('name', choice($.identifier, $.symbol)), ':', field('as', $._type_pattern)
    ),
    map_type: $ => seq('{',
      optional(seq($.map_type_item, repeat(seq(',', $.map_type_item)))), '}'
    ),

    // Nested element-type attr inside a pattern: pattern type, literal default
    // only (CT8v2). Keeping expressions out of patterns is what lets the whole
    // type sub-language move behind one scanner token.
    pattern_attr_type: $ => prec(1, seq(
      field('name', choice($.symbol, $.identifier)),
      ':', field('as', $._type_pattern),
      optional(seq('=', field('default', $._non_null_literal))),
    )),
    content_type: $ => seq(
      $._type_pattern,
      repeat(seq(',', $._type_pattern)),
    ),
    element_type: $ => seq('<', $.identifier, _attr_content_type($), '>'),
    fn_param: $ => seq(
      // param type is required
      field('name', $.identifier), seq(':', field('type', $._type_pattern)),
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
      // Delimited string/symbol patterns are self-contained type values.
      $.pattern_island,
    ),

    // Occurrence applied to primary type: T?, T+, T*, T[n]
    // No chaining allowed (like regex) - use explicit grouping: (T*)[2]
    // Use prec.dynamic(1) to prefer occurrence over concat_type continuation.
    occurrence_type: $ => prec.dynamic(1, prec.right(seq(
      field('operand', $.primary_type),
      field('operator', $.occurrence),
    ))),

    // Nullable elements bind before the array occurrence: `int?[]` is an
    // array of nullable int values, not an optional int array. The existing
    // occurrence builder receives the inner `int?` as its operand.
    nullable_array_type: $ => prec.dynamic(2, prec.right(seq(
      field('operand', $.occurrence_type),
      field('operator', $.occurrence_count),
    ))),

    // Prefix negation: !T (for string/symbol patterns: !\d)
    // Validated in AST builder for context-appropriate usage.
    negation_type: $ => prec.right(seq(
      '!', field('operand', $.primary_type),
    )),

    // Unary type: primary type with optional occurrence modifier
    // Replaces the old unary_type → _quantified_type chain
    unary_type: $ => prec.right(choice(
      $.nullable_array_type,
      $.occurrence_type,
      $.negation_type,          // !T - prefix negation
      $.primary_type,
    )),
    binary_type: $ => choice(
      ...type_pattern(choice($._type_pattern)),
    ),
    
    // Type pattern - the expression-free type sub-language (CT1v2).
    // Structural precedence (tightest to loosest):
    //   primary_type > occurrence_type > binary > fn_type
    // `that` (constrained_type) sits above this tier, and `^` never appears in
    // a value annotation: `T | error` is the sole spelling (CT3v2). `^`/`^E`
    // survive only on return types, where they declare the raised channel.
    _type_pattern: $ => choice(
      $.unary_type,            // covers primary_type and occurrence_type
      $.binary_type,           // alternation: T | U, T & U, T ! U
      $.fn_type,
    ),

    //  String/Symbol Pattern Island ----------------------------

    // The opening tag is one token so `\\symbol (` cannot be mistaken for a
    // tagged island with whitespace between the tag and its delimiter.
    _pattern_tag: _ => token(choice('\\symbol(', '\\(')),
    pattern_island: $ => seq(
      field('tag', $._pattern_tag),
      field('body', $._pattern_expr),
      ')'
    ),

    // Character classes are reserved only inside the pattern island.
    pattern_char_class: _ => choice(
      '...',   // any string
      'd',     // digit [0-9]
      'w',     // word [a-zA-Z0-9_]
      's',     // whitespace
      'a',     // alpha [a-zA-Z]
      '.',     // any character
    ),
    _pattern_primary_type: $ => choice(
      $.range_type,
      $._non_null_literal,
      $.identifier,
      $.pattern_char_class,
    ),
    pattern_occurrence_type: $ => prec.right(seq(
      field('operand', $._pattern_primary_type),
      field('operator', $.occurrence),
    )),
    pattern_negation_type: $ => prec.right(seq(
      '!', field('operand', $._pattern_primary_type),
    )),
    grouped_type: $ => prec.right(seq(
      optional('!'),
      '(', $._pattern_expr, ')',
      optional(field('occurrence', $.occurrence))
    )),

    // Whitespace concatenation exists only in the island. The old dynamic
    // precedence guard belonged to the open type grammar and is no longer
    // needed now that ordinary `int[2+]` has no concat alternative.
    concat_type: $ => prec.left(1, seq(
      choice($.pattern_unary_type, $.grouped_type),
      repeat1(choice($.pattern_unary_type, $.grouped_type)),
    )),
    string_binary_type: $ => choice(
      ...type_pattern(choice($._pattern_expr)),
    ),
    pattern_unary_type: $ => prec.right(choice(
      $.pattern_occurrence_type,
      $.pattern_negation_type,
      $._pattern_primary_type,
    )),
    _pattern_expr: $ => choice(
      $.pattern_unary_type,
      $.concat_type,
      alias($.string_binary_type, $.binary_type),
      $.grouped_type
    ),
};

module.exports = grammar({
  name: "lambda_full",
  ...common.options,
  // the shared core carries only the value-expression precedences; the type
  // tiers below need their own
  precedences: $ => [...common.options.precedences($), ...common.typePrecedences($)],
  rules: Object.assign({}, common.coreRules, fullTypeLayer),
});

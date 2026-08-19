/**
 * @file Lambda Script grammar for tree-sitter — PRODUCTION GRAMMAR
 * @author Henry Luo
 * @license MIT
 *
 * This is the grammar that ships. It shares its core with the official full
 * grammar (`grammar-lambda.js`) through `grammar-common.js`; the difference is
 * the type/path layer below. Full type patterns and string/symbol islands,
 * declaration return contracts, and whole view patterns arrive as opaque
 * tokens from the external scanner (src/scanner.c) and are parsed on the
 * Lambda side by lambda/runtime/parse_type_pattern.cpp. Paths retain their
 * lexical grammar boundary because `/` also means division and `.` also means
 * member access, but Lambda parses every recognized static path directly to
 * AST through lambda/runtime/parse_path_expr.cpp.
 *
 * Read grammar-lambda.js to learn what the type language accepts; it is the
 * normative statement. `make grammar-sync-check` guards the two against drift.
 */

// @ts-check
/// <reference types="../tree-sitter-dsl.d.ts" />

const common = require('./grammar-common.js');

// ---------------------------------------------------------------------------
// Type layer: external scanner tokens. The scanner only finds where each token
// ENDS; the interior is parsed by the Lambda-side hand parser.
// ---------------------------------------------------------------------------
const typeLayer = {
    // Annotation-position type pattern: `int`, `{a: int, b: [string]}`,
    // `fn(a: int) int`, `\(d[3])`, unions, occurrences — everything except the
    // `that` predicate, which the parser owns (CT1v2).
    _type_pattern: $ => $.type_pattern_token,

    // A single primary type: the `?T` query operand and view-pattern atoms.
    // Never consumes `|`, so `data?int | other` keeps `| other` as the value
    // union it is.
    _primary_type: $ => $.primary_type_pattern_token,
    // Retained only because grammar-common.js names the full-grammar helper.
    // Production view_pattern below supersedes every reachable use.
    _view_atom_type: $ => $.primary_type_pattern_token,

    // Declaration return contracts and view model patterns are bounded type
    // sub-forms with their own direct AST parsers.
    return_type: $ => $.return_type_token,
    view_pattern: $ => $.view_pattern_token,

    // Islands are first-class values (`let p = \(d[3])`), so they keep their
    // own token in value position.
    _value_island: $ => $.pattern_island_token,

    // Element/object content schema: a comma-separated list of patterns.
    // Content items use their own token so a bare field name (`type T { a: int }`)
    // is not swallowed as content before the parser sees the ':'.
    content_type: $ => seq(
      $.content_type_token, repeat(seq(',', $.content_type_token)),
    ),
};

module.exports = grammar({
  name: "lambda",
  ...common.options,
  // the scanner's tokens ride alongside the contextual `start` keyword
  externals: $ => [
    $._start,
    $.type_pattern_token,
    $.primary_type_pattern_token,
    $.pattern_island_token,
    $.content_type_token,
    $.return_type_token,
    $.view_pattern_token,
  ],
  rules: Object.assign({}, common.coreRules, typeLayer),
});

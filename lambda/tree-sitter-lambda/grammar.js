/**
 * @file Lambda Script grammar for tree-sitter — PRODUCTION GRAMMAR
 * @author Henry Luo
 * @license MIT
 *
 * This is the grammar that ships. It shares its core with the official full
 * grammar (`grammar-lambda.js`) through `grammar-common.js`; the difference is
 * the replacement layer below. Full type patterns and string/symbol islands,
 * declaration return contracts, whole view patterns, and path bodies arrive
 * as opaque tokens from the external scanner (src/scanner.c). Tree-sitter
 * retains only the unambiguous `/.` / `.` path introducer; Lambda parses the
 * full path span directly to AST through lambda/runtime/parse_path_expr.cpp.
 *
 * Read grammar-lambda.js for the complete structural surface grammar; it is the
 * normative statement. `make grammar-sync-check` guards the two against drift.
 */

// @ts-check
/// <reference types="../tree-sitter-dsl.d.ts" />

const common = require('./grammar-common.js');

// In element/attribute name position, commit to a qualified name before the
// parser shifts its first segment and can reinterpret the remaining `.name`
// as a relative path. The external token spans only that first segment; the
// rest stays structural so the existing dotted_name AST contract is retained.
function qualifiedName($, precedence) {
  return prec.left(precedence, seq(
    alias($.dotted_name_head_token, $.identifier),
    repeat1(seq('.', choice($.identifier, $.symbol))),
  ));
}

// ---------------------------------------------------------------------------
// Production replacement layer: external scanner tokens. The scanner only
// finds where each token ENDS; Lambda-side parsers own each interior.
// ---------------------------------------------------------------------------
const productionRuleLayer = {
    // Annotation-position type pattern: `int`, `{a: int, b: [string]}`,
    // `fn(a: int) int`, `\(d[3])`, unions, occurrences — everything except the
    // `that` predicate, which the parser owns (CT1v2).
    _type_pattern: $ => $.type_pattern_token,

    // A single primary type: the `?T` query operand and view-pattern primaries.
    // Never consumes `|`, so `data?int | other` keeps `| other` as the value
    // union it is.
    _primary_type: $ => $.primary_type_pattern_token,
    // Declaration return contracts and view model patterns are bounded type
    // sub-forms with their own direct AST parsers.
    return_type: $ => $.return_type_token,
    view_pattern: $ => $.view_pattern_token,

    // Keep the two rooted introducer characters as separate Tree-sitter tokens
    // so `/` retains its ordinary operator identity and `.` starts the path.
    // The complete dotted body is one scanner token parsed into AstPathNode.
    path_expr: $ => choice(
      seq('/', '.', $.path_body_token),
      seq('.', $.path_body_token),
    ),

    // Qualified element/attribute names need a contextual first-segment token
    // so `<svg.rect>` cannot become tag `svg` plus relative path `.rect`.
    _attr_dotted_name: $ => qualifiedName($, 51),
    dotted_name: $ => qualifiedName($, 49),

    // Islands are first-class values (`let p = \(d[3])`), so they keep their
    // own token in value position.
    _char_pattern: $ => $.pattern_island_token,

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
  // the scanner owns only the extracted sub-language boundaries
  externals: $ => [
    $.type_pattern_token,
    $.primary_type_pattern_token,
    $.pattern_island_token,
    $.content_type_token,
    $.return_type_token,
    $.view_pattern_token,
    $.path_body_token,
    $.dotted_name_head_token,
  ],
  // The production path keeps its introducer in grammar and makes the body an
  // external token. DOTTED_NAME_HEAD_TOKEN resolves the full grammar's
  // dotted-name/path ambiguity contextually, so only the query conflict stays.
  conflicts: $ => [
    [$._expr, $.query_expr],
  ],
  rules: Object.assign({}, common.coreRules, productionRuleLayer),
});

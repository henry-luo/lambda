#!/usr/bin/env bash
# Generate a parser from grammar-lambda.js, the OFFICIAL FULL grammar.
#
# The full grammar is the normative statement of Lambda's surface syntax: it
# spells out the type-pattern tiers and string/symbol islands that the shipped
# grammar hides behind external scanner tokens. Its parser is test-only — it
# backs `make test-grammar-diff`, which asserts the two grammars accept the same
# language. Everything it produces stays under ./temp/.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PKG="$ROOT/lambda/tree-sitter-lambda"
STAGE="$ROOT/temp/ts-lambda-full"
TS_CLI="${TS_CLI:-$ROOT/node_modules/.bin/tree-sitter}"

mkdir -p "$STAGE"
# the CLI generates into <cwd>/src, so stage a package of its own rather than
# letting it overwrite the production parser
cp "$PKG/grammar-common.js" "$STAGE/grammar-common.js"
cp "$PKG/grammar-lambda.js" "$STAGE/grammar.js"
sed 's/"name": "lambda"/"name": "lambda_full"/; s/"camelcase": "Lambda"/"camelcase": "LambdaFull"/' \
    "$PKG/tree-sitter.json" > "$STAGE/tree-sitter.json"

( cd "$STAGE" && "$TS_CLI" generate )
echo "✅ full grammar generated: $STAGE/src/parser.c"

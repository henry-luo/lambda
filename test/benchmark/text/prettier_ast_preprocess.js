// Build the compact parser output consumed by the cross-runtime printer benchmark.
const fs = require("fs");
const path = require("path");
const prettier = require("../../../ref/readability/source/node_modules/prettier");
const babel = require("../../../ref/readability/source/node_modules/prettier/plugins/babel");

const directory = __dirname;
const sourcePath = path.join(directory, "prettier_source.js");
const astPath = path.join(directory, "prettier_ast.json");

const omittedKeys = new Set([
  "loc",
  "range",
  "tokens",
  "errors",
  "comments",
  "leadingComments",
  "innerComments",
  "trailingComments",
  "extra",
]);

function compact(value) {
  if (Array.isArray(value)) return value.map(compact);
  if (value === null || typeof value !== "object") return value;

  const result = {};
  for (const [key, child] of Object.entries(value)) {
    if (omittedKeys.has(key) || child === undefined) continue;
    result[key] = compact(child);
  }
  return result;
}

async function main() {
  const source = fs.readFileSync(sourcePath, "utf8");
  const parsed = await babel.parsers.babel.parse(source, { parser: "babel" });
  const ast = compact(parsed.program);
  fs.writeFileSync(astPath, JSON.stringify(ast) + "\n");

  const formatted = await prettier.format(source, {
    parser: "babel",
    printWidth: 80,
    semi: true,
    singleQuote: false,
    trailingComma: "all",
  });
  process.stdout.write(formatted);
}

main().catch((error) => {
  process.stderr.write(String(error.stack || error) + "\n");
  process.exitCode = 1;
});

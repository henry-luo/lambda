// Benchmark a compact AST printer with a small Prettier-style document IR.
const fs = require("fs");

const AST_PATH = "test/benchmark/text/prettier_ast.json";
const PRINT_WIDTH = 80;
const ITERATIONS = 256;
const ast = JSON.parse(fs.readFileSync(AST_PATH, "utf8"));

function text(value) {
  return { kind: "text", value: String(value) };
}

function concat(...parts) {
  return { kind: "concat", parts: parts.flat() };
}

function join(separator, parts) {
  const result = [];
  parts.forEach((part, index) => {
    if (index > 0) result.push(separator);
    result.push(part);
  });
  return concat(result);
}

function indent(contents) {
  return { kind: "indent", contents };
}

function group(contents, breakThreshold = Infinity) {
  return { kind: "group", contents, breakThreshold };
}

function ifBreak(broken, flat = text("")) {
  return { kind: "if-break", broken, flat };
}

const line = { kind: "line" };
const softline = { kind: "softline" };
const hardline = { kind: "hardline" };

function flatLength(doc) {
  switch (doc.kind) {
    case "text":
      return doc.value.length;
    case "concat":
      return doc.parts.reduce((length, part) => length + flatLength(part), 0);
    case "indent":
      return flatLength(doc.contents);
    case "group":
      return flatLength(doc.contents);
    case "if-break":
      return flatLength(doc.flat);
    case "line":
      return 1;
    case "softline":
      return 0;
    case "hardline":
      return Infinity;
    default:
      return Infinity;
  }
}

function fits(doc, remaining) {
  return flatLength(doc) <= remaining;
}

function render(doc, state, mode = "break") {
  switch (doc.kind) {
    case "text":
      state.column += doc.value.length;
      return doc.value;
    case "concat": {
      let result = "";
      for (const part of doc.parts) result += render(part, state, mode);
      return result;
    }
    case "indent":
      state.indent += 1;
      const indented = render(doc.contents, state, mode);
      state.indent -= 1;
      return indented;
    case "group": {
      const flat = mode === "flat" ||
        (doc.breakThreshold >= flatLength(doc.contents) &&
          fits(doc.contents, state.width - state.column));
      return render(doc.contents, state, flat ? "flat" : "break");
    }
    case "if-break":
      return render(mode === "flat" ? doc.flat : doc.broken, state, mode);
    case "line":
      if (mode === "flat") {
        state.column += 1;
        return " ";
      }
      state.column = state.indent * 2;
      return "\n" + " ".repeat(state.column);
    case "softline":
      if (mode === "flat") return "";
      state.column = state.indent * 2;
      return "\n" + " ".repeat(state.column);
    case "hardline":
      state.column = state.indent * 2;
      return "\n" + " ".repeat(state.column);
    default:
      throw new Error("unknown document node: " + doc.kind);
  }
}

function renderDoc(doc) {
  return render(doc, { column: 0, indent: 0, width: PRINT_WIDTH }, "break");
}

function literal(node) {
  switch (node.type) {
    case "StringLiteral":
      return text(JSON.stringify(node.value));
    case "NumericLiteral":
      return text(node.value);
    case "BooleanLiteral":
      return text(node.value ? "true" : "false");
    case "NullLiteral":
      return text("null");
    case "RegExpLiteral":
      return text("/" + node.pattern + "/" + node.flags);
    default:
      throw new Error("unknown literal: " + node.type);
  }
}

function nodeKey(node) {
  if (node.computed) return concat(text("["), printNode(node.key), text("]"));
  return printNode(node.key);
}

function parameterList(params) {
  return group(
    concat(
      text("("),
      indent(concat(softline, join(concat(text(","), line), params.map(printNode)))),
      ifBreak(text(",")),
      softline,
      text(")"),
    ),
  );
}

function argumentList(args) {
  if (args.length === 0) return text("()");
  if (args.some((arg) => arg.type === "ObjectExpression")) {
    return concat(text("("), join(text(", "), args.map(printNode)), text(")"));
  }
  return group(
    concat(
      text("("),
      indent(concat(softline, join(concat(text(","), line), args.map(printNode)))),
      ifBreak(text(",")),
      softline,
      text(")"),
    ),
  );
}

function arrayDoc(elements) {
  if (elements.length === 0) return text("[]");
  return group(
    concat(
      text("["),
      indent(concat(softline, join(concat(text(","), line), elements.map(printNode)))),
      ifBreak(text(",")),
      softline,
      text("]"),
    ),
  );
}

function objectDoc(properties) {
  if (properties.length === 0) return text("{}");
  return group(
    concat(
      text("{"),
      indent(concat(line, join(concat(text(","), line), properties.map(printNode)), ifBreak(text(",")))),
      line,
      text("}"),
    ),
  );
}

function blockDoc(body) {
  if (body.length === 0) return text("{}");
  return concat(
    text("{"),
    indent(concat(hardline, join(hardline, body.map(printStatement)))),
    hardline,
    text("}"),
  );
}

function variableDoc(node, terminator) {
  const declaration = concat(
    text(node.kind + " "),
    join(concat(text(","), line), node.declarations.map(printNode)),
  );
  return terminator ? concat(declaration, text(";")) : declaration;
}

function printProperty(node) {
  if (node.type === "SpreadElement") return concat(text("..."), printNode(node.argument));
  const key = nodeKey(node);
  if (node.shorthand) return key;
  return concat(key, text(": "), printExpression(node.value));
}

function expressionPrecedence(node) {
  if (!node) return 100;
  if (node.type === "AssignmentExpression" || node.type === "ArrowFunctionExpression") return 1;
  if (node.type === "LogicalExpression") return node.operator === "&&" ? 3 : 2;
  if (node.type === "BinaryExpression") {
    if (node.operator === "*" || node.operator === "/" || node.operator === "%") return 12;
    if (node.operator === "+" || node.operator === "-") return 11;
    if (["<", "<=", ">", ">=", "in", "instanceof"].includes(node.operator)) return 9;
    if (["==", "!=", "===", "!=="].includes(node.operator)) return 8;
    return 7;
  }
  return 20;
}

function printExpression(node, parentPrecedence = 0) {
  const doc = printNode(node);
  return expressionPrecedence(node) < parentPrecedence
    ? concat(text("("), doc, text(")"))
    : doc;
}

function flattenAdditiveChain(node, operands) {
  if (node.type === "BinaryExpression" && node.operator === "+") {
    flattenAdditiveChain(node.left, operands);
    operands.push(node.right);
  } else {
    operands.push(node);
  }
}

function additiveChainDoc(node) {
  const operands = [];
  flattenAdditiveChain(node, operands);
  const tail = [];
  for (let index = 1; index < operands.length; index += 1) {
    tail.push(text(" +"), line, printExpression(operands[index], 12));
  }
  return group(concat(printExpression(operands[0], 11), indent(concat(tail))), 60);
}

function printNode(node) {
  if (!node) return text("");
  switch (node.type) {
    case "Identifier":
      return text(node.name);
    case "ThisExpression":
      return text("this");
    case "StringLiteral":
    case "NumericLiteral":
    case "BooleanLiteral":
    case "NullLiteral":
    case "RegExpLiteral":
      return literal(node);
    case "ArrayExpression":
      return arrayDoc(node.elements);
    case "ObjectExpression":
      return objectDoc(node.properties);
    case "ObjectProperty":
      return printProperty(node);
    case "VariableDeclarator":
      return node.init
        ? concat(printNode(node.id), text(" = "), printExpression(node.init))
        : printNode(node.id);
    case "SpreadElement":
      return concat(text("..."), printNode(node.argument));
    case "AssignmentPattern":
      return concat(printNode(node.left), text(" = "), printNode(node.right));
    case "ArrayPattern":
      return arrayDoc(node.elements);
    case "MemberExpression":
      return node.computed
        ? concat(printExpression(node.object, 20), text("["), printExpression(node.property), text("]"))
        : concat(printExpression(node.object, 20), text("."), printNode(node.property));
    case "CallExpression":
      if (node.arguments.length === 1 && node.arguments[0].type === "ArrowFunctionExpression") {
        const arrow = node.arguments[0];
        return group(
          concat(
            printExpression(node.callee, 20),
            text("("),
            parameterList(arrow.params),
            text(" =>"),
            indent(concat(line, printExpression(arrow.body))),
            ifBreak(text(",")),
            softline,
            text(")"),
          ),
        );
      }
      return concat(printExpression(node.callee, 20), argumentList(node.arguments));
    case "NewExpression":
      return concat(text("new "), printExpression(node.callee, 20), argumentList(node.arguments));
    case "BinaryExpression":
    case "LogicalExpression": {
      const precedence = expressionPrecedence(node);
      if (node.type === "BinaryExpression" && node.operator === "+" &&
          node.left.type === "BinaryExpression" && node.left.operator === "+") {
        return additiveChainDoc(node);
      }
      return group(
        concat(
          printExpression(node.left, precedence),
          text(" " + node.operator),
          indent(concat(line, printExpression(node.right, precedence + (node.operator === "&&" || node.operator === "||" ? 1 : 0)))),
        ),
      );
    }
    case "UnaryExpression":
      return node.operator === "!"
        ? concat(text("!"), printExpression(node.argument, 20))
        : concat(text(node.operator + " "), printExpression(node.argument, 20));
    case "AssignmentExpression":
      return concat(printExpression(node.left, 2), text(" " + node.operator + " "), printExpression(node.right, 1));
    case "ArrowFunctionExpression":
      return concat(parameterList(node.params), text(" => "), printExpression(node.body, 1));
    case "FunctionDeclaration":
      return concat(
        text((node.async ? "async " : "") + "function "),
        node.generator ? text("*") : text(""),
        printNode(node.id),
        parameterList(node.params),
        text(" "),
        blockDoc(node.body.body),
      );
    case "ClassDeclaration":
      return concat(text("class "), printNode(node.id), text(" "), printNode(node.body));
    case "ClassBody":
      return node.body.length === 0
        ? text("{}")
        : concat(
            text("{"),
            indent(concat(hardline, join(hardline, node.body.map(printNode)))),
            hardline,
            text("}"),
          );
    case "ClassMethod":
      return concat(
        node.static ? text("static ") : text(""),
        node.async ? text("async ") : text(""),
        node.generator ? text("*") : text(""),
        nodeKey(node),
        parameterList(node.params),
        text(" "),
        blockDoc(node.body.body),
      );
    default:
      return printStatement(node);
  }
}

function printStatement(node) {
  if (!node) return text("");
  switch (node.type) {
    case "VariableDeclaration":
      return variableDoc(node, true);
    case "ReturnStatement":
      return concat(text("return"), node.argument ? concat(text(" "), printNode(node.argument)) : text(""), text(";"));
    case "ExpressionStatement":
      return concat(printNode(node.expression), text(";"));
    case "BlockStatement":
      return blockDoc(node.body);
    case "IfStatement":
      if (node.consequent.type === "BlockStatement") {
        return concat(
          text("if ("),
          printExpression(node.test),
          text(") "),
          printStatement(node.consequent),
          node.alternate ? concat(text(" else "), printStatement(node.alternate)) : text(""),
        );
      }
      return group(
        concat(
          text("if ("),
          printExpression(node.test),
          text(")"),
          indent(concat(line, printStatement(node.consequent))),
          node.alternate ? indent(concat(line, text("else"), line, printStatement(node.alternate))) : text(""),
        ),
      );
    case "ForOfStatement":
      return concat(
        text("for ("),
        variableDoc(node.left, false),
        text(" of "),
        printNode(node.right),
        text(") "),
        printStatement(node.body),
      );
    case "FunctionDeclaration":
    case "ClassDeclaration":
      return printNode(node);
    case "ExportNamedDeclaration":
      if (node.declaration) return concat(text("export "), printStatement(node.declaration));
      return concat(
        text("export { "),
        join(text(", "), node.specifiers.map(printNode)),
        text(" };")
      );
    case "ExportSpecifier":
      return node.local.name === node.exported.name
        ? printNode(node.local)
        : concat(printNode(node.local), text(" as "), printNode(node.exported));
    default:
      throw new Error("unsupported statement: " + node.type);
  }
}

function printProgram(program) {
  return renderDoc(concat(join(hardline, program.body.map(printStatement)), hardline));
}

const __t0 = process.hrtime.bigint();
let formatted = "";
for (let iteration = 0; iteration < ITERATIONS; iteration += 1) {
  formatted = printProgram(ast);
}
const __t1 = process.hrtime.bigint();

let checksum = 0;
for (let index = 0; index < formatted.length; index += 1) {
  checksum = (checksum * 31 + formatted.charCodeAt(index)) % 1000000007;
}
process.stdout.write(formatted);
process.stdout.write("prettier_ast: " +
  (checksum === 56483873 ? "CHECKSUM:" + checksum : "FAIL checksum=" + checksum) + "\n");
process.stdout.write("__TIMING__:" + Number(__t1 - __t0) / 1e6 + "\n");

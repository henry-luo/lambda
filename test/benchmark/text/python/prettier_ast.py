#!/usr/bin/env python3
"""Compact AST document-printer workload from prettier_ast.js."""

import json
import sys
import time

AST_PATH = "test/benchmark/text/prettier_ast.json"
PRINT_WIDTH = 80
ITERATIONS = 256


class Doc:
    __slots__ = ("kind", "value", "parts", "contents", "broken", "flat", "threshold")

    def __init__(self, kind, value="", parts=None, contents=None,
                 broken=None, flat=None, threshold=float("inf")):
        self.kind = kind
        self.value = value
        self.parts = parts
        self.contents = contents
        self.broken = broken
        self.flat = flat
        self.threshold = threshold


def text(value):
    return Doc("text", value=str(value))


def concat(*parts):
    flattened = []
    for part in parts:
        if isinstance(part, list):
            flattened.extend(part)
        else:
            flattened.append(part)
    return Doc("concat", parts=flattened)


def join(separator, parts):
    result = []
    for index, part in enumerate(parts):
        if index:
            result.append(separator)
        result.append(part)
    return concat(result)


def indent(contents):
    return Doc("indent", contents=contents)


def group(contents, threshold=float("inf")):
    return Doc("group", contents=contents, threshold=threshold)


def if_break(broken, flat=None):
    return Doc("if-break", broken=broken, flat=flat if flat is not None else text(""))


line = Doc("line")
softline = Doc("softline")
hardline = Doc("hardline")


def flat_length(doc):
    kind = doc.kind
    if kind == "text":
        return len(doc.value)
    if kind == "concat":
        return sum(flat_length(part) for part in doc.parts)
    if kind in ("indent", "group"):
        return flat_length(doc.contents)
    if kind == "if-break":
        return flat_length(doc.flat)
    if kind == "line":
        return 1
    if kind == "softline":
        return 0
    return float("inf")


def render(doc, state, mode="break"):
    kind = doc.kind
    if kind == "text":
        state["column"] += len(doc.value)
        return doc.value
    if kind == "concat":
        return "".join(render(part, state, mode) for part in doc.parts)
    if kind == "indent":
        state["indent"] += 1
        value = render(doc.contents, state, mode)
        state["indent"] -= 1
        return value
    if kind == "group":
        flat = (mode == "flat" or
                (doc.threshold >= flat_length(doc.contents)
                 and flat_length(doc.contents) <= state["width"] - state["column"]))
        return render(doc.contents, state, "flat" if flat else "break")
    if kind == "if-break":
        return render(doc.flat if mode == "flat" else doc.broken, state, mode)
    if kind == "line" and mode == "flat":
        state["column"] += 1
        return " "
    if kind == "softline" and mode == "flat":
        return ""
    if kind in ("line", "softline", "hardline"):
        state["column"] = state["indent"] * 2
        return "\n" + " " * state["column"]
    raise ValueError(f"unknown document node: {kind}")


def render_doc(doc):
    return render(doc, {"column": 0, "indent": 0, "width": PRINT_WIDTH})


def literal(node):
    kind = node["type"]
    if kind == "StringLiteral":
        return text(json.dumps(node["value"], ensure_ascii=False))
    if kind == "NumericLiteral":
        return text(node["value"])
    if kind == "BooleanLiteral":
        return text("true" if node["value"] else "false")
    if kind == "NullLiteral":
        return text("null")
    if kind == "RegExpLiteral":
        return text("/" + node["pattern"] + "/" + node["flags"])
    raise ValueError(f"unknown literal: {kind}")


def node_key(node):
    if node.get("computed"):
        return concat(text("["), print_node(node["key"]), text("]"))
    return print_node(node["key"])


def parameter_list(params):
    return group(concat(
        text("("),
        indent(concat(softline, join(concat(text(","), line), [print_node(p) for p in params]))),
        if_break(text(",")), softline, text(")"),
    ))


def argument_list(args):
    if not args:
        return text("()")
    if any(arg["type"] == "ObjectExpression" for arg in args):
        return concat(text("("), join(text(", "), [print_node(arg) for arg in args]), text(")"))
    return group(concat(
        text("("),
        indent(concat(softline, join(concat(text(","), line), [print_node(arg) for arg in args]))),
        if_break(text(",")), softline, text(")"),
    ))


def array_doc(elements):
    if not elements:
        return text("[]")
    return group(concat(
        text("["),
        indent(concat(softline, join(concat(text(","), line), [print_node(e) for e in elements]))),
        if_break(text(",")), softline, text("]"),
    ))


def object_doc(properties):
    if not properties:
        return text("{}")
    return group(concat(
        text("{"),
        indent(concat(line, join(concat(text(","), line), [print_node(p) for p in properties]),
                      if_break(text(",")))),
        line, text("}"),
    ))


def block_doc(body):
    if not body:
        return text("{}")
    return concat(text("{"), indent(concat(hardline, join(hardline,
                  [print_statement(s) for s in body]))), hardline, text("}"))


def variable_doc(node, terminator):
    declaration = concat(text(node["kind"] + " "),
                         join(concat(text(","), line),
                              [print_node(d) for d in node["declarations"]]))
    return concat(declaration, text(";")) if terminator else declaration


def print_property(node):
    if node["type"] == "SpreadElement":
        return concat(text("..."), print_node(node["argument"]))
    key = node_key(node)
    return key if node.get("shorthand") else concat(key, text(": "), print_expression(node["value"]))


def expression_precedence(node):
    if not node:
        return 100
    kind = node["type"]
    if kind in ("AssignmentExpression", "ArrowFunctionExpression"):
        return 1
    if kind == "LogicalExpression":
        return 3 if node["operator"] == "&&" else 2
    if kind == "BinaryExpression":
        operator = node["operator"]
        if operator in ("*", "/", "%"):
            return 12
        if operator in ("+", "-"):
            return 11
        if operator in ("<", "<=", ">", ">=", "in", "instanceof"):
            return 9
        if operator in ("==", "!=", "===", "!=="):
            return 8
        return 7
    return 20


def print_expression(node, parent_precedence=0):
    doc = print_node(node)
    return concat(text("("), doc, text(")")) if expression_precedence(node) < parent_precedence else doc


def flatten_additive_chain(node, operands):
    if node["type"] == "BinaryExpression" and node["operator"] == "+":
        flatten_additive_chain(node["left"], operands)
        operands.append(node["right"])
    else:
        operands.append(node)


def additive_chain_doc(node):
    operands = []
    flatten_additive_chain(node, operands)
    tail = []
    for operand in operands[1:]:
        tail.extend((text(" +"), line, print_expression(operand, 12)))
    return group(concat(print_expression(operands[0], 11), indent(concat(tail))), 60)


def print_node(node):
    if not node:
        return text("")
    kind = node["type"]
    if kind == "Identifier":
        return text(node["name"])
    if kind == "ThisExpression":
        return text("this")
    if kind in ("StringLiteral", "NumericLiteral", "BooleanLiteral", "NullLiteral", "RegExpLiteral"):
        return literal(node)
    if kind == "ArrayExpression":
        return array_doc(node["elements"])
    if kind == "ObjectExpression":
        return object_doc(node["properties"])
    if kind == "ObjectProperty":
        return print_property(node)
    if kind == "VariableDeclarator":
        return (concat(print_node(node["id"]), text(" = "), print_expression(node["init"]))
                if node.get("init") else print_node(node["id"]))
    if kind == "SpreadElement":
        return concat(text("..."), print_node(node["argument"]))
    if kind == "AssignmentPattern":
        return concat(print_node(node["left"]), text(" = "), print_node(node["right"]))
    if kind == "ArrayPattern":
        return array_doc(node["elements"])
    if kind == "MemberExpression":
        return (concat(print_expression(node["object"], 20), text("["),
                       print_expression(node["property"]), text("]")) if node["computed"]
                else concat(print_expression(node["object"], 20), text("."),
                            print_node(node["property"])))
    if kind == "CallExpression":
        args = node["arguments"]
        if len(args) == 1 and args[0]["type"] == "ArrowFunctionExpression":
            arrow = args[0]
            return group(concat(
                print_expression(node["callee"], 20), text("("), parameter_list(arrow["params"]),
                text(" =>"), indent(concat(line, print_expression(arrow["body"]))),
                if_break(text(",")), softline, text(")"),
            ))
        return concat(print_expression(node["callee"], 20), argument_list(args))
    if kind == "NewExpression":
        return concat(text("new "), print_expression(node["callee"], 20), argument_list(node["arguments"]))
    if kind in ("BinaryExpression", "LogicalExpression"):
        precedence = expression_precedence(node)
        if (kind == "BinaryExpression" and node["operator"] == "+"
                and node["left"]["type"] == "BinaryExpression" and node["left"]["operator"] == "+"):
            return additive_chain_doc(node)
        increment = 1 if node["operator"] in ("&&", "||") else 0
        return group(concat(
            print_expression(node["left"], precedence), text(" " + node["operator"]),
            indent(concat(line, print_expression(node["right"], precedence + increment))),
        ))
    if kind == "UnaryExpression":
        return (concat(text("!"), print_expression(node["argument"], 20))
                if node["operator"] == "!"
                else concat(text(node["operator"] + " "), print_expression(node["argument"], 20)))
    if kind == "AssignmentExpression":
        return concat(print_expression(node["left"], 2), text(" " + node["operator"] + " "),
                      print_expression(node["right"], 1))
    if kind == "ArrowFunctionExpression":
        return concat(parameter_list(node["params"]), text(" => "), print_expression(node["body"], 1))
    if kind == "FunctionDeclaration":
        return concat(text(("async " if node.get("async") else "") + "function "),
                      text("*") if node.get("generator") else text(""), print_node(node["id"]),
                      parameter_list(node["params"]), text(" "), block_doc(node["body"]["body"]))
    if kind == "ClassDeclaration":
        return concat(text("class "), print_node(node["id"]), text(" "), print_node(node["body"]))
    if kind == "ClassBody":
        return (text("{}") if not node["body"] else
                concat(text("{"), indent(concat(hardline, join(hardline,
                       [print_node(member) for member in node["body"]]))), hardline, text("}")))
    if kind == "ClassMethod":
        return concat(text("static ") if node.get("static") else text(""),
                      text("async ") if node.get("async") else text(""),
                      text("*") if node.get("generator") else text(""),
                      node_key(node), parameter_list(node["params"]), text(" "),
                      block_doc(node["body"]["body"]))
    return print_statement(node)


def print_statement(node):
    if not node:
        return text("")
    kind = node["type"]
    if kind == "VariableDeclaration":
        return variable_doc(node, True)
    if kind == "ReturnStatement":
        return concat(text("return"), concat(text(" "), print_node(node["argument"]))
                      if node.get("argument") else text(""), text(";"))
    if kind == "ExpressionStatement":
        return concat(print_node(node["expression"]), text(";"))
    if kind == "BlockStatement":
        return block_doc(node["body"])
    if kind == "IfStatement":
        if node["consequent"]["type"] == "BlockStatement":
            return concat(text("if ("), print_expression(node["test"]), text(") "),
                          print_statement(node["consequent"]),
                          concat(text(" else "), print_statement(node["alternate"]))
                          if node.get("alternate") else text(""))
        return group(concat(
            text("if ("), print_expression(node["test"]), text(")"),
            indent(concat(line, print_statement(node["consequent"]))),
            indent(concat(line, text("else"), line, print_statement(node["alternate"])))
            if node.get("alternate") else text(""),
        ))
    if kind == "ForOfStatement":
        return concat(text("for ("), variable_doc(node["left"], False), text(" of "),
                      print_node(node["right"]), text(") "), print_statement(node["body"]))
    if kind in ("FunctionDeclaration", "ClassDeclaration"):
        return print_node(node)
    if kind == "ExportNamedDeclaration":
        if node.get("declaration"):
            return concat(text("export "), print_statement(node["declaration"]))
        return concat(text("export { "), join(text(", "),
                      [print_node(specifier) for specifier in node["specifiers"]]), text(" };"))
    if kind == "ExportSpecifier":
        return (print_node(node["local"]) if node["local"]["name"] == node["exported"]["name"]
                else concat(print_node(node["local"]), text(" as "), print_node(node["exported"])))
    raise ValueError(f"unsupported statement: {kind}")


def print_program(program):
    return render_doc(concat(join(hardline,
                             [print_statement(statement) for statement in program["body"]]), hardline))


def main():
    with open(AST_PATH, encoding="utf-8") as stream:
        ast = json.load(stream)
    started = time.perf_counter_ns()
    formatted = ""
    for _ in range(ITERATIONS):
        formatted = print_program(ast)
    elapsed_ms = (time.perf_counter_ns() - started) / 1_000_000

    checksum = 0
    for char in formatted:
        checksum = (checksum * 31 + ord(char)) % 1000000007
    sys.stdout.write(formatted)
    sys.stdout.write(f"prettier_ast: {'CHECKSUM' if checksum == 56483873 else 'FAIL checksum'}:{checksum}\n")
    sys.stdout.write(f"__TIMING__:{elapsed_ms:.3f}\n")
    if checksum != 56483873:
        raise SystemExit(1)


if __name__ == "__main__":
    main()

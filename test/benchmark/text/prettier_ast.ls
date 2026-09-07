// Benchmark the same compact AST printer and document IR under Lambda.

let ast_path = "test/benchmark/text/prettier_ast.json"
let print_width = 80
let iterations = 256
let large_length = 1000000000

let line_doc = {kind: "line"}
let softline_doc = {kind: "softline"}
let hardline_doc = {kind: "hardline"}

fn text_doc(value) => {kind: "text", value: string(value)}

fn concat_docs(parts) => {kind: "concat", parts: parts}

fn indent_doc(contents) => {kind: "indent", contents: contents}

fn group_doc(contents, break_threshold = large_length) =>
    {kind: "group", contents: contents, break_threshold: break_threshold}

fn if_break_doc(broken, flat = text_doc("")) =>
    {kind: "if-break", broken: broken, flat: flat}

fn join_docs_at(separator, parts, index, count, acc) {
    if (index >= count) concat_docs(acc)
    else {
        let next = if (index == 0) acc ++ [parts[index]]
            else acc ++ [separator, parts[index]]
        join_docs_at(separator, parts, index + 1, count, next)
    }
}

fn join_docs(separator, parts) => join_docs_at(separator, parts, 0, len(parts), [])

fn flat_length_at(doc, index, count, acc) {
    if (index >= count) acc
    else flat_length_at(doc, index + 1, count, acc + flat_length(doc.parts[index]))
}

fn flat_length(doc) {
    if (doc.kind == "text") len(doc.value)
    else if (doc.kind == "concat") flat_length_at(doc, 0, len(doc.parts), 0)
    else if (doc.kind == "indent") flat_length(doc.contents)
    else if (doc.kind == "group") flat_length(doc.contents)
    else if (doc.kind == "if-break") flat_length(doc.flat)
    else if (doc.kind == "line") 1
    else if (doc.kind == "softline") 0
    else large_length
}

fn fits(doc, remaining) => flat_length(doc) <= remaining

fn indent_string(level) {
    if (level <= 0) ""
    else "  " ++ indent_string(level - 1)
}

fn render_result(value, column) => {value: value, column: column}

fn render_parts_at(parts, index, count, ctx, mode, acc) {
    if (index >= count) render_result(acc, ctx.column)
    else {
        let rendered = render_doc(parts[index], ctx, mode)
        render_parts_at(parts, index + 1, count,
            {column: rendered.column, indent: ctx.indent}, mode,
            acc ++ rendered.value)
    }
}

fn render_doc(doc, ctx, mode) {
    if (doc.kind == "text") {
        render_result(doc.value, ctx.column + len(doc.value))
    } else if (doc.kind == "concat") {
        render_parts_at(doc.parts, 0, len(doc.parts), ctx, mode, "")
    } else if (doc.kind == "indent") {
        let rendered = render_doc(doc.contents,
            {column: ctx.column, indent: ctx.indent + 1}, mode)
        render_result(rendered.value, rendered.column)
    } else if (doc.kind == "group") {
        let is_flat = mode == "flat" or
            (doc.break_threshold >= flat_length(doc.contents) and
             fits(doc.contents, print_width - ctx.column))
        render_doc(doc.contents, ctx, if (is_flat) "flat" else "break")
    } else if (doc.kind == "if-break") {
        render_doc(if (mode == "flat") doc.flat else doc.broken, ctx, mode)
    } else if (doc.kind == "line") {
        if (mode == "flat") {
            render_result(" ", ctx.column + 1)
        } else {
            render_result("\n" ++ indent_string(ctx.indent), ctx.indent * 2)
        }
    } else if (doc.kind == "softline") {
        if (mode == "flat") render_result("", ctx.column)
        else render_result("\n" ++ indent_string(ctx.indent), ctx.indent * 2)
    } else {
        render_result("\n" ++ indent_string(ctx.indent), ctx.indent * 2)
    }
}

fn render_doc_root(doc) => render_doc(doc, {column: 0, indent: 0}, "break").value

fn json_quote(value) {
    let escaped_backslash = replace(value, "\\", "\\\\")
    let escaped_quote = replace(escaped_backslash, "\"", "\\\"")
    let escaped_newline = replace(escaped_quote, "\n", "\\n")
    let escaped_return = replace(escaped_newline, "\r", "\\r")
    let escaped_tab = replace(escaped_return, "\t", "\\t")
    "\"" ++ escaped_tab ++ "\""
}

fn literal_doc(node) {
    if (node.type == "StringLiteral") text_doc(json_quote(node.value))
    else if (node.type == "NumericLiteral") text_doc(node.value)
    else if (node.type == "BooleanLiteral") text_doc(if (node.value) "true" else "false")
    else if (node.type == "NullLiteral") text_doc("null")
    else text_doc("/" ++ node.pattern ++ "/" ++ node.flags)
}

fn print_nodes_at(nodes, index, count, acc) {
    if (index >= count) acc
    else {
        print_nodes_at(nodes, index + 1, count, acc ++ [print_node(nodes[index])])
    }
}

fn print_nodes(nodes) => print_nodes_at(nodes, 0, len(nodes), [])

fn print_statements_at(nodes, index, count, acc) {
    if (index >= count) acc
    else print_statements_at(nodes, index + 1, count, acc ++ [print_statement(nodes[index])])
}

fn print_statements(nodes) => print_statements_at(nodes, 0, len(nodes), [])

fn node_key(node) {
    if (node.computed) concat_docs([text_doc("["), print_node(node.key), text_doc("]")])
    else print_node(node.key)
}

fn parameter_list(params) {
    let printed = print_nodes(params)
    group_doc(concat_docs([
        text_doc("("),
        indent_doc(concat_docs([softline_doc, join_docs(concat_docs([text_doc(","), line_doc]), printed)])),
        if_break_doc(text_doc(",")),
        softline_doc,
        text_doc(")")
    ]))
}

fn has_object_argument(args, index, count) {
    if (index >= count) false
    else if (args[index].type == "ObjectExpression") true
    else has_object_argument(args, index + 1, count)
}

fn argument_list(args) {
    if (len(args) == 0) text_doc("()")
    else if (has_object_argument(args, 0, len(args)))
        concat_docs([text_doc("("), join_docs(text_doc(", "), print_nodes(args)), text_doc(")")])
    else group_doc(concat_docs([
        text_doc("("),
        indent_doc(concat_docs([softline_doc, join_docs(concat_docs([text_doc(","), line_doc]), print_nodes(args))])),
        if_break_doc(text_doc(",")),
        softline_doc,
        text_doc(")")
    ]))
}

fn array_doc(elements) {
    if (len(elements) == 0) text_doc("[]")
    else group_doc(concat_docs([
        text_doc("["),
        indent_doc(concat_docs([softline_doc, join_docs(concat_docs([text_doc(","), line_doc]), print_nodes(elements))])),
        if_break_doc(text_doc(",")),
        softline_doc,
        text_doc("]")
    ]))
}

fn object_doc(properties) {
    if (len(properties) == 0) text_doc("{}")
    else group_doc(concat_docs([
        text_doc("{"),
        indent_doc(concat_docs([line_doc, join_docs(concat_docs([text_doc(","), line_doc]), print_nodes(properties)), if_break_doc(text_doc(","))])),
        line_doc,
        text_doc("}")
    ]))
}

fn block_doc(body) {
    if (len(body) == 0) text_doc("{}")
    else concat_docs([
        text_doc("{"),
        indent_doc(concat_docs([hardline_doc, join_docs(hardline_doc, print_statements(body))])),
        hardline_doc,
        text_doc("}")
    ])
}

fn variable_doc(node, terminator) {
    let declaration = concat_docs([
        text_doc(node.kind ++ " "),
        join_docs(concat_docs([text_doc(","), line_doc]), print_nodes(node.declarations))
    ])
    if (terminator) concat_docs([declaration, text_doc(";")]) else declaration
}

fn print_property(node) {
    if (node.type == "SpreadElement") concat_docs([text_doc("..."), print_node(node.argument)])
    else {
        let key = node_key(node)
        if (node.shorthand) key
        else concat_docs([key, text_doc(": "), print_expression(node.value)])
    }
}

fn expression_precedence(node) {
    if (node == null) 100
    else if (node.type == "AssignmentExpression" or node.type == "ArrowFunctionExpression") 1
    else if (node.type == "LogicalExpression") if (node.operator == "&&") 3 else 2
    else if (node.type == "BinaryExpression") {
        if (node.operator == "*" or node.operator == "/" or node.operator == "%") 12
        else if (node.operator == "+" or node.operator == "-") 11
        else if (node.operator == "<" or node.operator == "<=" or node.operator == ">" or node.operator == ">=" or node.operator == "in" or node.operator == "instanceof") 9
        else if (node.operator == "==" or node.operator == "!=" or node.operator == "===" or node.operator == "!==") 8
        else 7
    } else 20
}

fn print_expression(node, parent_precedence = 0) {
    let doc = print_node(node)
    if (expression_precedence(node) < parent_precedence)
        concat_docs([text_doc("("), doc, text_doc(")")])
    else doc
}

fn flatten_additive(node, acc) {
    if (node.type == "BinaryExpression" and node.operator == "+")
        flatten_additive(node.left, acc) ++ [node.right]
    else acc ++ [node]
}

fn additive_tail_at(operands, index, count, acc) {
    if (index >= count) acc
    else additive_tail_at(operands, index + 1, count,
        acc ++ [text_doc(" +"), line_doc, print_expression(operands[index], 12)])
}

fn additive_chain_doc(node) {
    let operands = flatten_additive(node, [])
    group_doc(concat_docs([
        print_expression(operands[0], 11),
        indent_doc(concat_docs(additive_tail_at(operands, 1, len(operands), [])))
    ]), 60)
}

fn print_node(node) {
    if (node == null) text_doc("")
    else if (node.type == "Identifier") text_doc(node.name)
    else if (node.type == "ThisExpression") text_doc("this")
    else if (node.type == "StringLiteral" or node.type == "NumericLiteral" or
             node.type == "BooleanLiteral" or node.type == "NullLiteral" or
             node.type == "RegExpLiteral") literal_doc(node)
    else if (node.type == "ArrayExpression") array_doc(node.elements)
    else if (node.type == "ObjectExpression") object_doc(node.properties)
    else if (node.type == "ObjectProperty") print_property(node)
    else if (node.type == "VariableDeclarator") {
        if (node.init == null) print_node(node.id)
        else concat_docs([print_node(node.id), text_doc(" = "), print_expression(node.init)])
    }
    else if (node.type == "SpreadElement") { concat_docs([text_doc("..."), print_node(node.argument)]) }
    else if (node.type == "AssignmentPattern") { concat_docs([print_node(node.left), text_doc(" = "), print_node(node.right)]) }
    else if (node.type == "ArrayPattern") array_doc(node.elements)
    else if (node.type == "MemberExpression") {
        if (node.computed) concat_docs([print_expression(node.object, 20), text_doc("["), print_expression(node.property), text_doc("]")])
        else concat_docs([print_expression(node.object, 20), text_doc("."), print_node(node.property)])
    }
    else if (node.type == "CallExpression") {
        if (len(node.arguments) == 1 and node.arguments[0].type == "ArrowFunctionExpression") {
            let arrow = node.arguments[0]
            group_doc(concat_docs([
                print_expression(node.callee, 20), text_doc("("), parameter_list(arrow.params),
                text_doc(" =>"), indent_doc(concat_docs([line_doc, print_expression(arrow.body)])),
                if_break_doc(text_doc(",")), softline_doc, text_doc(")")
            ]))
        } else concat_docs([print_expression(node.callee, 20), argument_list(node.arguments)])
    }
    else if (node.type == "NewExpression") { concat_docs([text_doc("new "), print_expression(node.callee, 20), argument_list(node.arguments)]) }
    else if (node.type == "BinaryExpression" or node.type == "LogicalExpression") {
        let precedence = expression_precedence(node)
        if (node.type == "BinaryExpression" and node.operator == "+" and
            node.left.type == "BinaryExpression" and node.left.operator == "+") additive_chain_doc(node)
        else group_doc(concat_docs([
            print_expression(node.left, precedence),
            text_doc(" " ++ node.operator),
            indent_doc(concat_docs([line_doc, print_expression(node.right, precedence + if (node.operator == "&&" or node.operator == "||") 1 else 0)]))
        ]) )
    }
    else if (node.type == "UnaryExpression") {
        if (node.operator == "!") concat_docs([text_doc("!"), print_expression(node.argument, 20)])
        else concat_docs([text_doc(node.operator ++ " "), print_expression(node.argument, 20)])
    }
    else if (node.type == "AssignmentExpression") { concat_docs([print_expression(node.left, 2), text_doc(" " ++ node.operator ++ " "), print_expression(node.right, 1)]) }
    else if (node.type == "ArrowFunctionExpression") { concat_docs([parameter_list(node.params), text_doc(" => "), print_expression(node.body, 1)]) }
    else if (node.type == "FunctionDeclaration") { concat_docs([
        text_doc(if (node.async) "async function " else "function "),
        text_doc(if (node.generator) "*" else ""), print_node(node.id), parameter_list(node.params),
        text_doc(" "), block_doc(node.body.body)
    ]) }
    else if (node.type == "ClassDeclaration") { concat_docs([text_doc("class "), print_node(node.id), text_doc(" "), print_node(node.body)]) }
    else if (node.type == "ClassBody") {
        if (len(node.body) == 0) text_doc("{}")
        else concat_docs([text_doc("{"), indent_doc(concat_docs([hardline_doc, join_docs(hardline_doc, print_nodes(node.body))])), hardline_doc, text_doc("}")])
    }
    else if (node.type == "ClassMethod") { concat_docs([
        text_doc(if (node.static) "static " else ""), text_doc(if (node.async) "async " else ""),
        text_doc(if (node.generator) "*" else ""), node_key(node), parameter_list(node.params),
        text_doc(" "), block_doc(node.body.body)
    ]) }
    else print_statement(node)
}

fn print_statement(node) {
    if (node.type == "VariableDeclaration") variable_doc(node, true)
    else if (node.type == "ReturnStatement") concat_docs([text_doc("return"), if (node.argument == null) text_doc("") else concat_docs([text_doc(" "), print_node(node.argument)]), text_doc(";")])
    else if (node.type == "ExpressionStatement") concat_docs([print_node(node.expression), text_doc(";")])
    else if (node.type == "BlockStatement") block_doc(node.body)
    else if (node.type == "IfStatement") {
        if (node.consequent.type == "BlockStatement") concat_docs([
            text_doc("if ("), print_expression(node.test), text_doc(") "), print_statement(node.consequent),
            if (node.alternate == null) text_doc("") else concat_docs([text_doc(" else "), print_statement(node.alternate)])
        ])
        else group_doc(concat_docs([
            text_doc("if ("), print_expression(node.test), text_doc(")"),
            indent_doc(concat_docs([line_doc, print_statement(node.consequent)])),
            if (node.alternate == null) text_doc("") else indent_doc(concat_docs([line_doc, text_doc("else"), line_doc, print_statement(node.alternate)]))
        ]))
    }
    else if (node.type == "ForOfStatement") concat_docs([
        text_doc("for ("), variable_doc(node.left, false), text_doc(" of "), print_node(node.right), text_doc(") "), print_statement(node.body)
    ])
    else if (node.type == "FunctionDeclaration" or node.type == "ClassDeclaration") print_node(node)
    else if (node.type == "ExportNamedDeclaration") {
        if (node.declaration != null) concat_docs([text_doc("export "), print_statement(node.declaration)])
        else concat_docs([text_doc("export { "), join_docs(text_doc(", "), print_nodes(node.specifiers)), text_doc(" };")])
    }
    else if (node.type == "ExportSpecifier")
        if (node.local.name == node.exported.name) print_node(node.local)
        else concat_docs([print_node(node.local), text_doc(" as "), print_node(node.exported)])
    else text_doc("")
}

pn print_program(program) { render_doc_root(concat_docs([join_docs(hardline_doc, print_statements(program.body)), hardline_doc])) }

pn main() {
    let ast = input(ast_path, {type: "json"}) ^ { null }
    let t0 = clock()
    var formatted = ""
    var iteration = 0
    while (iteration < iterations) {
        formatted = print_program(ast)
        iteration = iteration + 1
    }
    let t1 = clock()
    var checksum: int = 0
    var index: int = 0
    while (index < len(formatted)) {
        checksum = (checksum * 31 + ord(formatted[index])) % 1000000007
        index = index + 1
    }
    print(formatted)
    if (checksum == 56483873) {
        print("prettier_ast: CHECKSUM:" ++ string(checksum) ++ "\n")
    } else {
        print("prettier_ast: FAIL checksum=" ++ string(checksum) ++ "\n")
    }
    print("__TIMING__:" ++ string((t1 - t0) * 1000.0) ++ "\n")
}

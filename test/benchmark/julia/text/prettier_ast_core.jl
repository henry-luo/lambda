# native port of test/benchmark/text/python/prettier_ast.py; see ../LICENSE.md.
# Compact AST document-printer workload from prettier_ast.js.
const AST_PATH = "test/benchmark/text/prettier_ast.json"
const PRINT_WIDTH = 80
const ITERATIONS = 256
abstract type A_Doc end
mutable struct C_Doc <: A_Doc
    kind::String
    value::String
    parts
    contents
    broken
    flat
    threshold::Float64
    C_Doc(::Val{:raw}) = new("", "", nothing, nothing, nothing, nothing, Inf)
end
function C_Doc(args...)
    self = C_Doc(Val(:raw))
    init_Doc(self, args...)
    return self
end

const C_Doc____slots__ = ("kind", "value", "parts", "contents", "broken", "flat", "threshold")
function init_Doc(self, kind, value="", parts=nothing, contents=nothing, broken=nothing, flat=nothing, threshold=Inf)
    self.kind = kind
    self.value = value
    self.parts = parts
    self.contents = contents
    self.broken = broken
    self.flat = flat
    self.threshold = threshold
    return nothing
end

function text(value)
    return C_Doc("text", string(value), nothing, nothing, nothing, nothing, Inf)
end

function concat(parts...)
    local flattened, part
    flattened = Any[]
    for part in parts
        if truth0((part isa AbstractVector))
            m_extend(flattened, part)
        else
            m_append(flattened, part)
        end
    end
    return C_Doc("concat", "", flattened, nothing, nothing, nothing, Inf)
end

function doc_join(separator, parts)
    local index, part, result
    result = Any[]
    for (index, part) in enumerate0(parts)
        if truth0(index)
            m_append(result, separator)
        end
        m_append(result, part)
    end
    return concat(result)
end

function indent(contents)
    return C_Doc("indent", "", nothing, contents, nothing, nothing, Inf)
end

function group(contents, threshold=Inf)
    return C_Doc("group", "", nothing, contents, nothing, nothing, threshold)
end

function if_break(broken, flat=nothing)
    return C_Doc("if-break", "", nothing, nothing, broken, (truth0(((flat !== nothing))) ? flat : text("")), Inf)
end

const line = C_Doc("line")
const softline = C_Doc("softline")
const hardline = C_Doc("hardline")
function flat_length(doc)
    local kind, part
    kind = doc.kind
    if truth0(((kind == "text")))
        return Base.length(doc.value)
    end
    if truth0(((kind == "concat")))
        return sum((flat_length(part) for part in doc.parts); init=0)
    end
    if truth0((in0(kind, ("indent", "group"))))
        return flat_length(doc.contents)
    end
    if truth0(((kind == "if-break")))
        return flat_length(doc.flat)
    end
    if truth0(((kind == "line")))
        return 1
    end
    if truth0(((kind == "softline")))
        return 0
    end
    return Inf
end

function render(doc, state, mode="break")
    local flat, kind, part, value
    kind = doc.kind
    if truth0(((kind == "text")))
        set0!(state, "column", add0(get0(state, "column"), Base.length(doc.value)))
        return doc.value
    end
    if truth0(((kind == "concat")))
        return m_join("", (render(part, state, mode) for part in doc.parts))
    end
    if truth0(((kind == "indent")))
        set0!(state, "indent", add0(get0(state, "indent"), 1))
        value = render(doc.contents, state, mode)
        set0!(state, "indent", (get0(state, "indent") - 1))
        return value
    end
    if truth0(((kind == "group")))
        flat = (let _bool_value = ((mode == "flat")); truth0(_bool_value) ? _bool_value : (let _bool_value = ((doc.threshold >= flat_length(doc.contents))); truth0(_bool_value) ? ((flat_length(doc.contents) <= (get0(state, "width") - get0(state, "column")))) : _bool_value end) end)
        return render(doc.contents, state, (truth0(flat) ? "flat" : "break"))
    end
    if truth0(((kind == "if-break")))
        return render((truth0(((mode == "flat"))) ? doc.flat : doc.broken), state, mode)
    end
    if truth0((let _bool_value = ((kind == "line")); truth0(_bool_value) ? ((mode == "flat")) : _bool_value end))
        set0!(state, "column", add0(get0(state, "column"), 1))
        return " "
    end
    if truth0((let _bool_value = ((kind == "softline")); truth0(_bool_value) ? ((mode == "flat")) : _bool_value end))
        return ""
    end
    if truth0((in0(kind, ("line", "softline", "hardline"))))
        set0!(state, "column", mul0(get0(state, "indent"), 2))
        return add0("\n", mul0(" ", get0(state, "column")))
    end
    throw(ErrorException(string("unknown document node: ", string(kind))))
end

function render_doc(doc)
    return render(doc, Dict{Any,Any}("column"=>0, "indent"=>0, "width"=>PRINT_WIDTH))
end

function literal(node)
    local kind
    kind = get0(node, "type")
    if truth0(((kind == "StringLiteral")))
        return text(json_string(get0(node, "value")))
    end
    if truth0(((kind == "NumericLiteral")))
        return text(get0(node, "value"))
    end
    if truth0(((kind == "BooleanLiteral")))
        return text((truth0(get0(node, "value")) ? "true" : "false"))
    end
    if truth0(((kind == "NullLiteral")))
        return text("null")
    end
    if truth0(((kind == "RegExpLiteral")))
        return text(add0(add0(add0("/", get0(node, "pattern")), "/"), get0(node, "flags")))
    end
    throw(ErrorException(string("unknown literal: ", string(kind))))
end

function node_key(node)
    if truth0(m_get(node, "computed"))
        return concat(text("["), print_node(get0(node, "key")), text("]"))
    end
    return print_node(get0(node, "key"))
end

function parameter_list(params)
    local p
    return group(concat(text("("), indent(concat(softline, doc_join(concat(text(","), line), [print_node(p) for p in params]))), if_break(text(",")), softline, text(")")))
end

function argument_list(args)
    local arg
    if truth0(!truth0(args))
        return text("()")
    end
    if truth0(any((((get0(arg, "type") == "ObjectExpression")) for arg in args)))
        return concat(text("("), doc_join(text(", "), [print_node(arg) for arg in args]), text(")"))
    end
    return group(concat(text("("), indent(concat(softline, doc_join(concat(text(","), line), [print_node(arg) for arg in args]))), if_break(text(",")), softline, text(")")))
end

function array_doc(elements)
    local e
    if truth0(!truth0(elements))
        return text("[]")
    end
    return group(concat(text("["), indent(concat(softline, doc_join(concat(text(","), line), [print_node(e) for e in elements]))), if_break(text(",")), softline, text("]")))
end

function object_doc(properties)
    local p
    if truth0(!truth0(properties))
        return text("{}")
    end
    return group(concat(text("{"), indent(concat(line, doc_join(concat(text(","), line), [print_node(p) for p in properties]), if_break(text(",")))), line, text("}")))
end

function block_doc(body)
    local s
    if truth0(!truth0(body))
        return text("{}")
    end
    return concat(text("{"), indent(concat(hardline, doc_join(hardline, [print_statement(s) for s in body]))), hardline, text("}"))
end

function variable_doc(node, terminator)
    local d, declaration
    declaration = concat(text(add0(get0(node, "kind"), " ")), doc_join(concat(text(","), line), [print_node(d) for d in get0(node, "declarations")]))
    return (truth0(terminator) ? concat(declaration, text(";")) : declaration)
end

function print_property(node)
    local key
    if truth0(((get0(node, "type") == "SpreadElement")))
        return concat(text("..."), print_node(get0(node, "argument")))
    end
    key = node_key(node)
    return (truth0(m_get(node, "shorthand")) ? key : concat(key, text(": "), print_expression(get0(node, "value"))))
end

function expression_precedence(node)
    local kind, operator
    if truth0(!truth0(node))
        return 100
    end
    kind = get0(node, "type")
    if truth0((in0(kind, ("AssignmentExpression", "ArrowFunctionExpression"))))
        return 1
    end
    if truth0(((kind == "LogicalExpression")))
        return (truth0(((get0(node, "operator") == "&&"))) ? 3 : 2)
    end
    if truth0(((kind == "BinaryExpression")))
        operator = get0(node, "operator")
        if truth0((in0(operator, ("*", "/", "%"))))
            return 12
        end
        if truth0((in0(operator, ("+", "-"))))
            return 11
        end
        if truth0((in0(operator, ("<", "<=", ">", ">=", "in", "instanceof"))))
            return 9
        end
        if truth0((in0(operator, ("==", "!=", "===", "!=="))))
            return 8
        end
        return 7
    end
    return 20
end

function print_expression(node, parent_precedence=0)
    local doc
    doc = print_node(node)
    return (truth0(((expression_precedence(node) < parent_precedence))) ? concat(text("("), doc, text(")")) : doc)
end

function flatten_additive_chain(node, operands)
    if truth0((let _bool_value = ((get0(node, "type") == "BinaryExpression")); truth0(_bool_value) ? ((get0(node, "operator") == "+")) : _bool_value end))
        flatten_additive_chain(get0(node, "left"), operands)
        m_append(operands, get0(node, "right"))
    else
        m_append(operands, node)
    end
    return nothing
end

function additive_chain_doc(node)
    local operand, operands, tail
    operands = Any[]
    flatten_additive_chain(node, operands)
    tail = Any[]
    for operand in slice0(operands, 1, nothing, nothing)
        m_extend(tail, (text(" +"), line, print_expression(operand, 12)))
    end
    return group(concat(print_expression(get0(operands, 0), 11), indent(concat(tail))), 60)
end

function print_node(node)
    local args, arrow, increment, kind, member, precedence
    if truth0(!truth0(node))
        return text("")
    end
    kind = get0(node, "type")
    if truth0(((kind == "Identifier")))
        return text(get0(node, "name"))
    end
    if truth0(((kind == "ThisExpression")))
        return text("this")
    end
    if truth0((in0(kind, ("StringLiteral", "NumericLiteral", "BooleanLiteral", "NullLiteral", "RegExpLiteral"))))
        return literal(node)
    end
    if truth0(((kind == "ArrayExpression")))
        return array_doc(get0(node, "elements"))
    end
    if truth0(((kind == "ObjectExpression")))
        return object_doc(get0(node, "properties"))
    end
    if truth0(((kind == "ObjectProperty")))
        return print_property(node)
    end
    if truth0(((kind == "VariableDeclarator")))
        return (truth0(m_get(node, "init")) ? concat(print_node(get0(node, "id")), text(" = "), print_expression(get0(node, "init"))) : print_node(get0(node, "id")))
    end
    if truth0(((kind == "SpreadElement")))
        return concat(text("..."), print_node(get0(node, "argument")))
    end
    if truth0(((kind == "AssignmentPattern")))
        return concat(print_node(get0(node, "left")), text(" = "), print_node(get0(node, "right")))
    end
    if truth0(((kind == "ArrayPattern")))
        return array_doc(get0(node, "elements"))
    end
    if truth0(((kind == "MemberExpression")))
        return (truth0(get0(node, "computed")) ? concat(print_expression(get0(node, "object"), 20), text("["), print_expression(get0(node, "property")), text("]")) : concat(print_expression(get0(node, "object"), 20), text("."), print_node(get0(node, "property"))))
    end
    if truth0(((kind == "CallExpression")))
        args = get0(node, "arguments")
        if truth0((let _bool_value = ((Base.length(args) == 1)); truth0(_bool_value) ? ((get0(get0(args, 0), "type") == "ArrowFunctionExpression")) : _bool_value end))
            arrow = get0(args, 0)
            return group(concat(print_expression(get0(node, "callee"), 20), text("("), parameter_list(get0(arrow, "params")), text(" =>"), indent(concat(line, print_expression(get0(arrow, "body")))), if_break(text(",")), softline, text(")")))
        end
        return concat(print_expression(get0(node, "callee"), 20), argument_list(args))
    end
    if truth0(((kind == "NewExpression")))
        return concat(text("new "), print_expression(get0(node, "callee"), 20), argument_list(get0(node, "arguments")))
    end
    if truth0((in0(kind, ("BinaryExpression", "LogicalExpression"))))
        precedence = expression_precedence(node)
        if truth0((let _bool_value = ((kind == "BinaryExpression")); truth0(_bool_value) ? (let _bool_value = ((get0(node, "operator") == "+")); truth0(_bool_value) ? (let _bool_value = ((get0(get0(node, "left"), "type") == "BinaryExpression")); truth0(_bool_value) ? ((get0(get0(node, "left"), "operator") == "+")) : _bool_value end) : _bool_value end) : _bool_value end))
            return additive_chain_doc(node)
        end
        increment = (truth0((in0(get0(node, "operator"), ("&&", "||")))) ? 1 : 0)
        return group(concat(print_expression(get0(node, "left"), precedence), text(add0(" ", get0(node, "operator"))), indent(concat(line, print_expression(get0(node, "right"), add0(precedence, increment))))))
    end
    if truth0(((kind == "UnaryExpression")))
        return (truth0(((get0(node, "operator") == "!"))) ? concat(text("!"), print_expression(get0(node, "argument"), 20)) : concat(text(add0(get0(node, "operator"), " ")), print_expression(get0(node, "argument"), 20)))
    end
    if truth0(((kind == "AssignmentExpression")))
        return concat(print_expression(get0(node, "left"), 2), text(add0(add0(" ", get0(node, "operator")), " ")), print_expression(get0(node, "right"), 1))
    end
    if truth0(((kind == "ArrowFunctionExpression")))
        return concat(parameter_list(get0(node, "params")), text(" => "), print_expression(get0(node, "body"), 1))
    end
    if truth0(((kind == "FunctionDeclaration")))
        return concat(text(add0((truth0(m_get(node, "async")) ? "async " : ""), "function ")), (truth0(m_get(node, "generator")) ? text("*") : text("")), print_node(get0(node, "id")), parameter_list(get0(node, "params")), text(" "), block_doc(get0(get0(node, "body"), "body")))
    end
    if truth0(((kind == "ClassDeclaration")))
        return concat(text("class "), print_node(get0(node, "id")), text(" "), print_node(get0(node, "body")))
    end
    if truth0(((kind == "ClassBody")))
        return (truth0(!truth0(get0(node, "body"))) ? text("{}") : concat(text("{"), indent(concat(hardline, doc_join(hardline, [print_node(member) for member in get0(node, "body")]))), hardline, text("}")))
    end
    if truth0(((kind == "ClassMethod")))
        return concat((truth0(m_get(node, "static")) ? text("static ") : text("")), (truth0(m_get(node, "async")) ? text("async ") : text("")), (truth0(m_get(node, "generator")) ? text("*") : text("")), node_key(node), parameter_list(get0(node, "params")), text(" "), block_doc(get0(get0(node, "body"), "body")))
    end
    return print_statement(node)
end

function print_statement(node)
    local kind, specifier
    if truth0(!truth0(node))
        return text("")
    end
    kind = get0(node, "type")
    if truth0(((kind == "VariableDeclaration")))
        return variable_doc(node, true)
    end
    if truth0(((kind == "ReturnStatement")))
        return concat(text("return"), (truth0(m_get(node, "argument")) ? concat(text(" "), print_node(get0(node, "argument"))) : text("")), text(";"))
    end
    if truth0(((kind == "ExpressionStatement")))
        return concat(print_node(get0(node, "expression")), text(";"))
    end
    if truth0(((kind == "BlockStatement")))
        return block_doc(get0(node, "body"))
    end
    if truth0(((kind == "IfStatement")))
        if truth0(((get0(get0(node, "consequent"), "type") == "BlockStatement")))
            return concat(text("if ("), print_expression(get0(node, "test")), text(") "), print_statement(get0(node, "consequent")), (truth0(m_get(node, "alternate")) ? concat(text(" else "), print_statement(get0(node, "alternate"))) : text("")))
        end
        return group(concat(text("if ("), print_expression(get0(node, "test")), text(")"), indent(concat(line, print_statement(get0(node, "consequent")))), (truth0(m_get(node, "alternate")) ? indent(concat(line, text("else"), line, print_statement(get0(node, "alternate")))) : text(""))))
    end
    if truth0(((kind == "ForOfStatement")))
        return concat(text("for ("), variable_doc(get0(node, "left"), false), text(" of "), print_node(get0(node, "right")), text(") "), print_statement(get0(node, "body")))
    end
    if truth0((in0(kind, ("FunctionDeclaration", "ClassDeclaration"))))
        return print_node(node)
    end
    if truth0(((kind == "ExportNamedDeclaration")))
        if truth0(m_get(node, "declaration"))
            return concat(text("export "), print_statement(get0(node, "declaration")))
        end
        return concat(text("export { "), doc_join(text(", "), [print_node(specifier) for specifier in get0(node, "specifiers")]), text(" };"))
    end
    if truth0(((kind == "ExportSpecifier")))
        return (truth0(((get0(get0(node, "local"), "name") == get0(get0(node, "exported"), "name")))) ? print_node(get0(node, "local")) : concat(print_node(get0(node, "local")), text(" as "), print_node(get0(node, "exported"))))
    end
    throw(ErrorException(string("unsupported statement: ", string(kind))))
end

function print_program(program)
    local statement
    return render_doc(concat(doc_join(hardline, [print_statement(statement) for statement in get0(program, "body")]), hardline))
end


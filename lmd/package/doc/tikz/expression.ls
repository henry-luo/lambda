// Evaluate the parser's finite PGF math expression tree, never Lambda source.
fn binding(context, variable) {
    let matches = if (context == null) []
        else [for (entry in context.variables where entry.name == variable) entry.value]
    if (len(matches) == 0) null else matches[len(matches) - 1]
}

fn declared_function(context, function_name) {
    let matches = if (context == null) []
        else [for (entry in context.functions where entry.name == function_name) entry]
    if (len(matches) == 0) null else matches[len(matches) - 1]
}

fn has_radian_mark(node) {
    if (not (node is element)) false
    else if (string(name(node)) == "radian_value") true
    else len([for (child in node where has_radian_mark(child)) child]) > 0
}

pub fn evaluate(node, x, context = null, function_depth = 0) float^ {
    let kind = string(name(node))
    if (kind == "number_literal") float(node.value)
    else if (kind == "variable") {
        if (node.name == "x") float(x)
        else if (node.name == "pi") 3.141592653589793
        else {
            let value = binding(context, node.name)
            if (value == null) raise error("unknown plot variable: " ++ node.name)
            else float(value)
        }
    }
    else if (kind == "unary") {
        let operand = evaluate(node[0], x, context, function_depth)^
        if (node.op == "-") 0.0 - operand else operand
    }
    else if (kind == "radian_value") evaluate(node[0], x, context, function_depth)^
    else if (kind == "binary") {
        let lhs = evaluate(node[0], x, context, function_depth)^
        let rhs = evaluate(node[1], x, context, function_depth)^
        let op = node.op
        if (op == "+") lhs + rhs
        else if (op == "-") lhs - rhs
        else if (op == "*") lhs * rhs
        else if (op == "/") {
            if (rhs == 0.0) raise error("division by zero in PGFPlots expression")
            else lhs / rhs
        }
        else if (op == "^") lhs ** rhs
        else if (op == ">") if (lhs > rhs) 1.0 else 0.0
        else if (op == "<") if (lhs < rhs) 1.0 else 0.0
        else if (op == ">=") if (lhs >= rhs) 1.0 else 0.0
        else if (op == "<=") if (lhs <= rhs) 1.0 else 0.0
        else if (op == "==") if (lhs == rhs) 1.0 else 0.0
        else if (op == "!=") if (lhs != rhs) 1.0 else 0.0
        else raise error("unknown PGFPlots arithmetic operator")
    }
    else if (kind == "conditional") {
        // PGF conditionals evaluate only the selected branch.
        if (evaluate(node[0], x, context, function_depth)^ != 0.0)
            evaluate(node[1], x, context, function_depth)^
        else evaluate(node[2], x, context, function_depth)^
    }
    else if (kind == "function_call") {
        let function_name = node.name
        let arity = len(content(node))
        let valid_arity = if (arity != 1 and
            not ((function_name == "divide" or function_name == "pow") and
                arity == 2))
            raise error("unsupported PGF math function arity: " ++ function_name)
            else true
        let argument = evaluate(node[0], x, context, function_depth)^
        // PGF's postfix r may bind inside a product such as sin(pi*x r).
        let radians = if (has_radian_mark(node[0])) argument
            else argument * 3.141592653589793 / 180.0
        if (function_name == "sin") math.sin(radians)
        else if (function_name == "cos") math.cos(radians)
        else if (function_name == "tan") math.tan(radians)
        else if (function_name == "exp") math.exp(argument)
        else if (function_name == "ln") {
            if (argument <= 0.0) raise error("ln requires a positive argument")
            else math.log(argument)
        }
        else if (function_name == "log10") {
            if (argument <= 0.0) raise error("log10 requires a positive argument")
            else math.log(argument) / math.log(10.0)
        }
        else if (function_name == "sqrt") {
            if (argument < 0.0) raise error("sqrt requires a nonnegative argument")
            else math.sqrt(argument)
        }
        else if (function_name == "abs") abs(argument)
        else if (function_name == "deg") argument * 180.0 / 3.141592653589793
        else if (function_name == "rad") argument * 3.141592653589793 / 180.0
        else if (function_name == "divide") {
            let denominator = evaluate(node[1], x, context, function_depth)^
            if (denominator == 0.0) raise error("division by zero in PGF math")
            else argument / denominator
        }
        else if (function_name == "pow")
            argument ** evaluate(node[1], x, context, function_depth)^
        else {
            let definition = declared_function(context, function_name)
            if (definition == null) raise error("unknown PGFPlots function: " ++ function_name)
            else if (function_depth >= 16) raise error("PGFPlots function recursion limit")
            else {
                let local = {functions: context.functions,
                    variables: [*context.variables, {name: "#1", value: argument}]}
                evaluate(definition.tree, argument, local, function_depth + 1)^
            }
        }
    }
    else raise error("invalid PGFPlots expression tree")
}

// PGFPlots discards undefined function samples; other evaluator failures remain errors.
pub fn undefined_sample(message) => message == "ln requires a positive argument" or
    message == "log10 requires a positive argument" or
    message == "sqrt requires a nonnegative argument" or
    message == "division by zero in PGFPlots expression" or
    message == "division by zero in PGF math"

pub fn evaluate_sample(node, x, context = null) any^ {
    evaluate(node, x, context) ^ {
        if (undefined_sample(^.message)) null
        else raise ^
    }
}

// Evaluate the parser's finite PGF math expression tree, never Lambda source.
pub fn evaluate(node, x) float^ {
    let kind = string(name(node))
    if (kind == "number_literal") float(node.value)
    else if (kind == "variable") {
        if (node.name == "x") float(x)
        else if (node.name == "pi") 3.141592653589793
        else raise error("unknown plot variable")
    }
    else if (kind == "unary") {
        let operand = evaluate(node[0], x)^
        if (node.op == "-") 0.0 - operand else operand
    }
    else if (kind == "binary") {
        let lhs = evaluate(node[0], x)^
        let rhs = evaluate(node[1], x)^
        let op = node.op
        if (op == "+") lhs + rhs
        else if (op == "-") lhs - rhs
        else if (op == "*") lhs * rhs
        else if (op == "/") {
            if (rhs == 0.0) raise error("division by zero in PGFPlots expression")
            else lhs / rhs
        }
        else if (op == "^") lhs ** rhs
        else raise error("unknown PGFPlots arithmetic operator")
    }
    else if (kind == "function_call") {
        let argument = evaluate(node[0], x)^
        let radians = argument * 3.141592653589793 / 180.0
        let function_name = node.name
        if (function_name == "sin") math.sin(radians)
        else if (function_name == "cos") math.cos(radians)
        else if (function_name == "tan") math.tan(radians)
        else if (function_name == "exp") math.exp(argument)
        else if (function_name == "ln") {
            if (argument <= 0.0) raise error("ln requires a positive argument")
            else math.log(argument)
        }
        else if (function_name == "sqrt") {
            if (argument < 0.0) raise error("sqrt requires a nonnegative argument")
            else math.sqrt(argument)
        }
        else if (function_name == "abs") abs(argument)
        else if (function_name == "deg") argument * 180.0 / 3.141592653589793
        else if (function_name == "rad") argument * 3.141592653589793 / 180.0
        else raise error("unknown PGFPlots function")
    }
    else raise error("invalid PGFPlots expression tree")
}

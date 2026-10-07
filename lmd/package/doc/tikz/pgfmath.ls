// Bounded PGFPlots math declarations and numeric foreach accumulators.
import expression: .expression
import opts: .options
import util: lambda.latex.util

pub fn expression_tree(source) any^ {
    let parsed = parse("\\addplot{" ++ trim(source) ++ "};", {type: "tikz"})^
    let plots = [for (child in parsed
        where child is element and string(name(child)) == "plot") child]
    let valid_plot = if (len(plots) != 1)
        raise error("PGF math expression did not produce one plot") else true
    let trees = [for (child in plots[0]
        where child is element and string(name(child)) != "option") child]
    if (len(trees) != 1) raise error("PGF math expression tree is missing")
    else trees[0]
}

fn declared_function(source) any^ {
    let entry = trim(source)
    let open_at = index_of(entry, "(")
    let close_at = index_of(entry, ")")
    let equal_at = index_of(entry, "=")
    let valid_signature = if (open_at == null or close_at == null or equal_at == null or
        close_at <= open_at or equal_at <= close_at or
        trim(slice(entry, open_at + 1, close_at)) != "\\x")
        raise error("PGF declare function requires one \\x parameter") else true
    let function_name = trim(slice(entry, 0, open_at))
    let valid_name = if (function_name == "")
        raise error("PGF function name is missing") else true
    {name: function_name,
     tree: expression_tree(slice(entry, equal_at + 1, len(entry)))^}
}

fn declared_functions(picture) any^ {
    let raw = if (picture == null) null
        else opts.value(picture, "declare function", null)
    if (raw == null) []
    else {
        let clean = util.strip_tex_comments(raw)
        let parts = util.split_top_level(clean, ";");
        [for (part in parts where trim(part) != "") declared_function(part)^]
    }
}

fn external_function(declaration) any^ {
    let valid = if (trim(declaration.arity) != "1" or
        trim(declaration.name) == "")
        raise error("PGF math declaration needs a name and one argument")
        else true
    let body = trim(util.strip_tex_comments(declaration.source))
    let prefix = "\\pgfmathparse"
    let valid_body = if (not starts_with(body, prefix))
        raise error("PGF math function body needs \\pgfmathparse") else true
    let grouped = util.read_balanced(body, len(prefix), "{", "}")^
    let valid_tail = if (trim(slice(body, grouped.next, len(body))) != "")
        raise error("unsupported PGF math function body") else true
    {name: trim(declaration.name), tree: expression_tree(grouped.raw)^}
}

fn external_step(declaration) any^ {
    let target = trim(declaration.target)
    let valid = if (not starts_with(target, "\\") or
        len(target) <= 1 or declaration.source == null)
        raise error("PGF math setmacro needs a control-sequence target")
        else true
    {kind: "setmacro", target: slice(target, 1, len(target)),
     tree: expression_tree(declaration.source)^}
}

// Document-local declarations are parsed once and evaluated in source order.
pub fn external_program(declarations) any^ =>
    {external: true,
     functions: [for (entry in declarations where entry.kind == "function")
        external_function(entry)^],
     steps: [for (entry in declarations where entry.kind == "setmacro")
        external_step(entry)^]}

pub fn with_definition(program_data, node) any^ =>
    {*:program_data, steps: [*program_data.steps, program_step(node)^]}

pub fn evaluate_source(source, program_data, x = 0.0) float^ {
    let tree = expression_tree(util.unwrap_braces(trim(source)))^
    let context = context_at(for_expression(program_data, tree), x)^
    expression.evaluate(tree, x, context)^
}

fn skip_text_space(source, at) =>
    if (at >= len(source)) at
    else if (slice(source, at, at + 1) == " " or
             slice(source, at, at + 1) == "\n" or
             slice(source, at, at + 1) == "\t")
        skip_text_space(source, at + 1)
    else at

fn tikzmath_assignment(source) any^ {
    let body = trim(source)
    let valid = if (not starts_with(body, "let "))
        raise error("TikZ math branch needs a let assignment") else true
    let equal_at = index_of(body, "=")
    let valid_equal = if (equal_at == null)
        raise error("TikZ math let assignment needs =") else true
    let target = trim(slice(body, 4, equal_at))
    let rhs = trim(slice(body, equal_at + 1, len(body)))
    let expression_source = if (ends_with(rhs, ";"))
        trim(slice(rhs, 0, len(rhs) - 1)) else rhs
    let valid_target = if (not starts_with(target, "\\") or
        len(target) < 2 or expression_source == "")
        raise error("unsupported TikZ math assignment") else true
    {kind: "setmacro", target: slice(target, 1, len(target)),
     tree: expression_tree(expression_source)^}
}

// The math library's bounded conditional assigns one scalar to later paths.
pub fn with_tikzmath(program_data, source) any^ {
    let body = trim(util.strip_tex_comments(source))
    let then_at = index_of(body, "then")
    let valid = if (not starts_with(body, "if ") or then_at == null)
        raise error("unsupported TikZ math statement") else true
    let condition = trim(slice(body, 3, then_at))
    let first_at = skip_text_space(body, then_at + 4)
    let true_group = util.read_balanced(body, first_at, "{", "}")^
    let else_at = skip_text_space(body, true_group.next)
    let valid_else = if (slice(body, else_at, else_at + 4) != "else")
        raise error("TikZ math conditional needs else") else true
    let second_at = skip_text_space(body, else_at + 4)
    let false_group = util.read_balanced(body, second_at, "{", "}")^
    let remaining = trim(slice(body, false_group.next, len(body)))
    let valid_tail = if (remaining != ";" and remaining != "")
        raise error("unsupported trailing TikZ math statements") else true
    let branch = if (evaluate_source(condition, program_data)^ != 0.0)
        true_group.raw else false_group.raw
    let step = tikzmath_assignment(branch)^;
    {*:program_data, steps: [*program_data.steps, step]}
}

fn command_end(source, at) {
    if (at >= len(source)) at
    else if (index_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ",
                     slice(source, at, at + 1)) != null)
        command_end(source, at + 1)
    else at
}

fn foreach_body(raw) any^ {
    let body = trim(util.strip_tex_comments(raw))
    let prefix = "\\xdef\\"
    let valid_prefix = if (not starts_with(body, prefix))
        raise error("PGFPlots foreach body requires an xdef accumulator") else true
    let end_at = command_end(body, len(prefix))
    let variable = slice(body, len(prefix), end_at)
    let remainder = trim(slice(body, end_at, len(body)))
    let group = util.read_balanced(remainder, 0, "{", "}")^
    let valid_body = if (variable == "" or
        trim(slice(remainder, group.next, len(remainder))) != "")
        raise error("unsupported PGFPlots foreach body") else true
    {target: variable, tree: expression_tree(group.raw)^}
}

fn foreach_step(node) any^ {
    let parts = [for (part in split(trim(node.range), ",")) trim(part)]
    let valid_parts = if (len(parts) != 3 or parts[1] != "...")
        raise error("PGFPlots foreach requires a numeric start,...,end range") else true
    let low = opts.numeric_value(parts[0])^
    let high = opts.numeric_value(parts[2])^
    let valid_range = if (float(int(low)) != low or float(int(high)) != high or
        low < 0.0 or high < low or high - low > 255.0)
        raise error("PGFPlots foreach range must be 0..255 with at most 256 items") else true
    let body = foreach_body(node.body)^;
    {kind: "foreach", target: body.target, tree: body.tree,
     low: int(low), high: int(high)}
}

fn program_step(node) any^ {
    let tag = string(name(node))
    if (tag == "pgf_definition")
        {kind: node.kind, target: node.variable,
         tree: expression_tree(node.source)^}
    else foreach_step(node)^
}

pub fn program(picture, axis_node) any^ {
    let functions = declared_functions(picture)^
    let steps = [for (child in axis_node
        where child is element and
            (string(name(child)) == "pgf_definition" or
             string(name(child)) == "pgfplots_foreach")) program_step(child)^];
    {functions: functions, steps: steps}
}

fn referenced_variables(node, functions, depth = 0) {
    if (node is array or node is list)
        [for (child in node, variable in referenced_variables(child, functions, depth)) variable]
    else if (not (node is element)) []
    else if (string(name(node)) == "variable") [node.name]
    else {
        let direct = [for (child in content(node), variable in referenced_variables(child, functions, depth)) variable]
        let definitions = [for (definition in functions where definition.name == node.name) definition]
        let called = if (string(name(node)) != "function_call" or depth >= 16) []
            else [for (definition in definitions,
                variable in referenced_variables(definition.tree, functions, depth + 1)) variable];
        [*direct, *called]
    }
}

fn named(names, target) =>
    len([for (candidate in names where candidate == target) candidate]) > 0

fn dependencies(steps, functions, names, depth) {
    if (depth > len(steps)) names
    else {
        let used = [for (step in steps where named(names, step.target)) step]
        let found = [for (step in used, variable in referenced_variables(step.tree, functions))
            variable]
        let added = [for (variable in found where not named(names, variable)) variable]
        if (len(added) == 0) names
        else dependencies(steps, functions, [*names, *added], depth + 1)
    }
}

// A plot only evaluates definitions reachable from its expression variables.
pub fn for_expression(program_data, tree) {
    if (program_data == null) null
    else {
        let direct = referenced_variables(tree, program_data.functions)
        let names = dependencies(program_data.steps, program_data.functions, direct, 0)
        {*:program_data, steps: [for (step in program_data.steps
            where named(names, step.target)) step]}
    }
}

// An accumulator has one live binding; retaining every prior value makes
// repeated function sampling quadratic in the foreach range.
fn assigned_variables(variables, target, value) {
    let present = len([for (entry in variables where entry.name == target) entry]) > 0
    let updated = [for (entry in variables)
        if (entry.name == target) {name: target, value: value} else entry]
    if (present) updated else [*variables, {name: target, value: value}]
}

fn accumulate(step, x, index, context) any^ {
    if (index > step.high) context
    else {
        let local = {*:context, variables: [*context.variables,
            {name: "#1", value: float(index)}]}
        let value = expression.evaluate(step.tree, x, local)^
        let next = {*:context, variables:
            assigned_variables(context.variables, step.target, value)}
        accumulate(step, x, index + 1, next)^
    }
}

fn execute_steps(steps, x, at, context) any^ {
    if (at >= len(steps)) context
    else {
        let step = steps[at]
        let next = if (step.kind == "foreach")
            accumulate(step, x, step.low, context)^
        else {
            let value = expression.evaluate(step.tree, x, context)^;
            {*:context, variables:
                assigned_variables(context.variables, step.target, value)}
        }
        execute_steps(steps, x, at + 1, next)^
    }
}

pub fn context_at(program_data, x, sample_variable = null) any^ =>
    if (program_data == null) null
    else {
        let initial = {functions: program_data.functions,
            variables: if (sample_variable == null) [] else
                [{name: sample_variable, value: float(x)}]}
        execute_steps(program_data.steps, x, 0, initial)^
    }

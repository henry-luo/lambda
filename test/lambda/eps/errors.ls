import eps: lambda.doc.eps.eps

fn doc(body) any^ => parse("%!PS-Adobe-3.0 EPSF-3.0\n%%BoundingBox: 0 0 10 10\n%%EndComments\n" ++ body, 'eps')^

// S7.6.7v4: procedure handlers are statements; capture diagnostics through a var.
pn diagnose(body, options = null) string^ {
    var message = ""
    eps.interpret(doc(body)^, options) ^ { message = ^.message }
    return message
}

pn main() {
    let underflow = diagnose("moveto")^
    let unknown = diagnose("missing_operator")^
    let types = diagnose("true 2 add")^
    let infinite = diagnose("{} loop", {limits: {max_steps: 100}})^
    let recursion = diagnose("/p {p} def p", {limits: {max_execution_depth: 20}})^
    let syntax = diagnose("<gg>")^
    let invalid_access = diagnose("[1] readonly 0 2 put")^
    let dead = eps.interpret(doc("false {missing_operator} if")^)^
    print({underflow: contains(underflow, "stackunderflow"), unknown: contains(unknown, "missing_operator"),
        types: contains(types, "typecheck"), infinite: contains(infinite, "limitcheck"),
        recursion: contains(recursion, "limitcheck"), syntax: contains(syntax, "syntaxerror"),
        access: contains(invalid_access, "invalidaccess"), dead_branch: len(dead.stack) == 0})
}

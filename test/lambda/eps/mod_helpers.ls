import eps: lambda.doc.eps.eps

pub fn doc(body, size = 10) any^ => parse("%!PS-Adobe-3.0 EPSF-3.0\n%%BoundingBox: 0 0 " ++
    string(size) ++ " " ++ string(size) ++ "\n%%EndComments\n" ++ body, 'eps')^

// S7.6.7v4: procedure handlers are statements; capture diagnostics through a var.
pub pn diagnose(body, options = null) string^ {
    var message = ""
    eps.interpret(doc(body)^, options) ^ { message = ^.message }
    return message
}

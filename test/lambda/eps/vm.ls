import eps: lambda.doc.eps.eps

fn doc(body) any^ => parse("%!PS-Adobe-3.0 EPSF-3.0\n%%BoundingBox: 0 0 100 100\n%%EndComments\n" ++ body, 'eps')^

pn main() {
    let display = eps.interpret(doc("/a [1] def /s (ABC) def /saved save def\n" ++
        "a 0 9 put s 0 90 put /temporary 77 def 1 0 0 setrgbcolor 0 0 10 10 rectfill\n" ++
        "saved restore a 0 get 1 eq s 0 get 90 eq\n" ++
        "/temporary where {pop false} {true} ifelse (same) (same) eq\n")^)^
    var invalid = ""
    eps.interpret(doc("save [] exch restore")^) ^ { invalid = ^.message }
    var denied = ""
    eps.interpret(doc("2 dict readonly begin /x 1 def")^) ^ { denied = ^.message }
    print({checks: len(display.stack), passed: all(display.stack), preserved_paint: len(display.draws) == 1,
        invalid_restore: contains(invalid, "invalidrestore"), readonly_dictionary: contains(denied, "invalidaccess")})
}

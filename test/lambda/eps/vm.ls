import eps: lambda.doc.eps.eps
import helpers: .mod_helpers

pn main() {
    let display = eps.interpret(helpers.doc("/a [1] def /s (ABC) def /saved save def\n" ++
        "a 0 9 put s 0 90 put /temporary 77 def 1 0 0 setrgbcolor 0 0 10 10 rectfill\n" ++
        "saved restore a 0 get 1 eq s 0 get 90 eq\n" ++
        "/temporary where {pop false} {true} ifelse (same) (same) eq\n", 100)^)^
    var invalid = ""
    eps.interpret(helpers.doc("save [] exch restore", 100)^) ^ { invalid = ^.message }
    var denied = ""
    eps.interpret(helpers.doc("2 dict readonly begin /x 1 def", 100)^) ^ { denied = ^.message }
    print({checks: len(display.stack), passed: all(display.stack), preserved_paint: len(display.draws) == 1,
        invalid_restore: contains(invalid, "invalidrestore"), readonly_dictionary: contains(denied, "invalidaccess")})
}

import eps: lambda.doc.eps.eps
import helpers: .mod_helpers

pn main() {
    let display = eps.interpret(helpers.doc("0.0 pop 256 3 mul dup type /integertype eq exch string length 768 eq\n" ++
        "[1 2.5] dup 0 get type /integertype eq exch 1 get type /realtype eq\n" ++
        "1.0 1 exch pop type /integertype eq\n" ++
        "3 2 idiv type /integertype eq 3 2 mod type /integertype eq\n" ++
        "0 1 2 {type /integertype eq} for\n")^)^
    print({checks:len(display.stack), passed:all(display.stack)})
}

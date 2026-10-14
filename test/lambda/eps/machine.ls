import eps: lambda.doc.eps.eps

pn main() {
    let source = "%!PS-Adobe-3.0 EPSF-3.0\n%%BoundingBox: 0 0 100 100\n%%EndComments\n" ++
        "/a [1 2 add] def /b a def b 0 9 put a 0 get 9 eq\n" ++
        "/s 3 string def /v s 1 1 getinterval def v 0 65 put s 1 get 65 eq\n" ++
        "/r {add} bind def /add {pop pop 99} def 2 3 r 5 eq\n" ++
        "/n 7 def /p {//n} def /n 8 def p 7 eq\n" ++
        "0 1 1 5 {systemdict /add get exec} for 15 eq\n" ++
        "0 3 {1 systemdict /add get exec} repeat 3 eq\n" ++
        "{42 exit} loop 42 eq\n" ++
        "[1 2 3] {2 mul} forall 6 eq 3 1 roll 4 eq exch 2 eq\n" ++
        "true {11} {22} ifelse 11 eq\n"
    let doc = parse(source, 'eps')^
    let display = eps.interpret(doc)^
    print({checks: len(display.stack), passed: all(display.stack), draws: len(display.draws)})
}

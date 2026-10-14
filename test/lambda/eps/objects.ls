import eps: lambda.doc.eps.eps

pn main() {
    let source = "%!PS-Adobe-3.0 EPSF-3.0\n%%BoundingBox: 0 0 10 10\n%%EndComments\n" ++
        "2147483647 1 add 2147483648.0 eq\n" ++
        "2147483647 2147483647 mul type /realtype eq\n" ++
        "2147483647 1 2147483648 {dup 2147483648.0 eq {type /realtype eq} {pop} ifelse} for\n" ++
        "[1 2] 4 array copy length 2 eq\n" ++
        "/a [1] def /ro a readonly def a 0 2 put ro 0 get 2 eq\n" ++
        "a 0 a length getinterval a eq\n" ++
        "systemdict /add get xcheck true eq\n" ++
        "systemdict /add get cvlit xcheck false eq\n" ++
        "2 3 systemdict /add get cvlit cvx exec 5 eq\n"
    let display = eps.interpret(parse(source, 'eps')^)^
    print({checks: len(display.stack), passed: all(display.stack)})
}

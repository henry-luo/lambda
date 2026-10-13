import eps: lambda.doc.eps.eps

pn main() {
    let display = eps.interpret(input("test/input/eps/dictionary-prolog.eps", 'eps')^)^
    let picture = eps.display_to_svg(display)^
    print({checks: len(display.stack), passed: all(display.stack), draws: len(display.draws),
        blue: contains(format(picture, 'xml')^, "rgb(0,0,255)")})
}

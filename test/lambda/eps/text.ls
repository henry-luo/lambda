import eps: lambda.doc.eps.eps
import helpers: .mod_helpers

pn main() {
    let display = eps.interpret(input("test/input/eps/text-graphics.eps", 'eps')^)^
    let measured = eps.interpret(helpers.doc("/Courier 12 selectfont 2 3 moveto (AB) stringwidth pop 0 gt currentpoint 3 eq exch 2 eq (AB) show currentpoint 3 eq exch 2 gt")^)^
    let misplaced = helpers.diagnose("(x) show")^
    let unsupported = helpers.diagnose("0 0 moveto <ff> show")^
    print({draws:len(display.draws), text:contains(format(eps.display_to_svg(display)^,'xml')^,"Palatino"),
        width_and_point:all(measured.stack), currentpoint:contains(misplaced,"nocurrentpoint"),
        encoding:contains(unsupported,"StandardEncoding")})
}

import eps: lambda.doc.eps.eps

pn main() {
    let source = "%!PS-Adobe-3.0 EPSF-3.0\n%%BoundingBox: 0 0 100 100\n%%EndComments\n" ++
        "newpath 1 2 moveto 10 20 translate 3 4 lineto currentpoint 4 eq exch 3 eq stroke\n" ++
        "gsave 2 3 scale 1 1 transform 23 eq exch 12 eq grestore\n" ++
        "matrix currentmatrix 4 get 10 eq\n" ++
        "5 7 matrix translate /t exch def 1 2 t transform 9 eq exch 6 eq\n" ++
        "1 2 t itransform -5 eq exch -4 eq\n" ++
        "newpath 0 0 10 0 360 arc stroke\n"
    let display = eps.interpret(parse(source, 'eps')^)^
    print({checks: len(display.stack), passed: all(display.stack), draws: len(display.draws),
        frozen_path: display.draws[0].node.d == "M -9 -18 L 3 4",
        paint_matrix: display.draws[0].matrix == [1.0, 0.0, 0.0, 1.0, 10.0, 20.0],
        cubic_arc: contains(display.draws[1].node.d, "C ")})
}

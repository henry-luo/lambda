import eps: lambda.doc.eps.eps
import helpers: .mod_helpers

pn main() {
    let source = "currentfont /FontName get /Courier eq\n" ++
        "/f /Times-Roman findfont def /Times-Roman findfont f eq\n" ++
        "f type /dicttype eq f wcheck not f /FontName get /Times-Roman eq\n" ++
        "f /FID get type /fonttype eq\n" ++
        "f 12 scalefont /scaled exch def scaled /FontMatrix get 0 get .012 sub abs 1e-12 lt\n" ++
        "f /FontMatrix get 0 get .001 sub abs 1e-12 lt scaled /FID get f /FID get eq\n" ++
        "scaled setfont currentfont scaled eq\n" ++
        "gsave /Courier 8 selectfont currentfont /FontName get /Courier eq grestore currentfont scaled eq\n" ++
        "f [20 5 7 30 4 8] makefont /transformed exch def\n" ++
        "transformed /FontMatrix get dup 0 get .02 sub abs 1e-12 lt exch 4 get 4 eq\n" ++
        "transformed setfont /saved save def /Courier-Bold findfont 9 scalefont setfont saved restore currentfont transformed eq\n" ++
        "save /Helvetica-Oblique findfont pop restore /Helvetica-Oblique findfont /FontName get /Helvetica-Oblique eq\n" ++
        "/Helvetica-Bold [12 0 0 16 0 0] selectfont currentfont /FontMatrix get 3 get .016 sub abs 1e-12 lt\n" ++
        "[/Helvetica /Helvetica-Bold /Helvetica-Oblique /Helvetica-BoldOblique\n" ++
        " /Times-Roman /Times-Bold /Times-Italic /Times-BoldItalic\n" ++
        " /Courier /Courier-Bold /Courier-Oblique /Courier-BoldOblique /Symbol /ZapfDingbats]\n" ++
        "{dup findfont /FontName get eq} forall\n"
    let display = eps.interpret(helpers.doc(source)^)^
    let drawing = eps.interpret(input("test/input/eps/font-selection.eps", 'eps')^)^
    let missing = helpers.diagnose("/MissingFont findfont")^
    let invalid = helpers.diagnose("10 dict setfont")^
    let matrix = helpers.diagnose("/Times-Roman findfont [1 2] makefont")^
    let readonly = helpers.diagnose("/Times-Roman findfont /FontType 42 put")^
    let discarded = helpers.diagnose("save /Times-Roman findfont /FID get exch restore")^
    let overflow = helpers.diagnose("/Times-Roman findfont 1e308 scalefont 1e308 scalefont")^
    let text = helpers.diagnose("/Times-Roman findfont 12 scalefont setfont 0 0 moveto (x) show")^
    print({checks: len(display.stack), passed: all(display.stack), draws: len(drawing.draws),
        missing: contains(missing, "invalidfont"), invalid: contains(invalid, "invalidfont"),
        matrix: contains(matrix, "typecheck"), readonly: contains(readonly, "invalidaccess"),
        discarded: contains(discarded, "invalidrestore"), overflow: contains(overflow, "invalidfont"),
        text_profile: contains(text, "unsupported: show")})
}

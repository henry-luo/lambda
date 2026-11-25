// D10.2.2v2: standalone TeX math and file input use the Script engine.
let source = "\\def\\square#1{#1^2}\\frac{\\square{x}}{2}";
let expected = <math <fraction cmd:"\\frac", numer:<group <subsup base:"x", sup:"2">>, denom:<group "2">>>;
let file = "test/lambda/math/fixtures/script_standalone_math.tex";
[
    parse(source, {type:"math", flavor:"latex"})^ == expected,
    parse(source, "math-latex")^ == expected,
    input(file, {type:"math", flavor:"latex"})^ == expected,
    parse("", "math")^ == null,
    parse("x+y", {type:"math", flavor:"ascii"})^ is <math>
]

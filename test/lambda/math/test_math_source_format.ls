// Source-format compatibility fixtures captured before the C++ retirement.
let sources = [
    "x_i^2+\\alpha",
    "\\frac{a+b}{c}",
    "\\sqrt[3]{x}",
    "\\binom{n}{k}",
    "\\genfrac{(}{)}{2pt}{0}{a}{b}",
    "{a\\above2pt b}",
    "\\sum\\limits_{i=0}^n i",
    "\\left( x+1 \\right)",
    "\\hat{x}+\\overline{a}",
    "\\text{A\\&B \\$5}",
    "\\operatorname*{argmax}_x",
    "\\mathbf{x}+\\displaystyle y",
    "\\begin{array}{cc}a&b\\\\[2pt]c&d\\end{array}",
    "\\overset{a}{b}+\\underset{x}{y}",
    "\\xrightarrow[below]{above}",
    "\\includegraphics[width=1em]{image.png}",
    "\\bigl( x \\bigr)",
    "\\textcolor{red}{x}+\\fcolorbox{blue}{red}{y}",
    "\\boxed{x}+\\phantom{y}",
    "\\mathop{abc}",
    "\\mathchoice{a}{b}{c}{d}",
    "\\mathrel{=}",
    "\\matrix{a&b\\\\c&d}",
    "\\rule[1pt]{2pt}{3pt}",
    "\\not\\in+\\not{}",
    "\\raisebox{1em}[2em][1em]{x}",
    "\\tag*{A}+\\pmod{n}",
    "\\verb*|a b|",
    "\\quad a\\,b",
    "\\unknowncommand{a}",
    "sin(x)+lim_{x\\to0}x",
    "\\varepsilon+\\times+\\leq+\\infty+\\sin{x}",
    "\\begin{alignedat}{1}x&=y\\end{alignedat}",
    "\\tiny x",
    "\\text{a $x^2$ b}"
];
[for (source in sources) {
    let ast = parse(source, 'math')^;
    [format(ast, {type:"math", flavor:"latex"})^, format(ast, {type:"math", flavor:"ascii"})^]
}]

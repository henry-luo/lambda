// D10.2.2v2: execute the bundled package's real equality implementation.
let cases=["\\usepackage{ifthen}\\ifthenelse{\\equal{a}{a}}{alpha}{other}",
    "\\usepackage{ifthen}\\ifthenelse{\\equal{b}{a}}{alpha}{other}"];
fn show(src) any^ {let r=parse(src,{type:"tex",adapters:[]})^; {text:r.text,diagnostics:r.diagnostics}}
[for(src in cases) show(src)^]

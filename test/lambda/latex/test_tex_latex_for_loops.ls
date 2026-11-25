// S1.8, D10.2.2v2: loop bodies and definitions execute as TeX tokens.
let cases = [
    "\\makeatletter\\@for\\x:=a,{b,c},d\\do{[\\x]}\\makeatother",
    "\\makeatletter\\@for\\x:=a,,b,\\do{[\\x]}\\makeatother",
    "\\makeatletter\\def\\items{u,v}\\@for\\x:=\\items,w\\do{[\\x]}\\makeatother",
    "\\makeatletter\\@for\\x:=\\do{[\\x]}END\\makeatother",
    "\\makeatletter\\@tfor\\x:=a {bc} d\\do{[\\x]}\\makeatother",
    "\\makeatletter\\@for\\x:=one,two\\do{\\@for\\y:=A,B\\do{[\\x\\y]}}\\makeatother"
];
fn run(source) any^ {
    let result = parse(source, "tex")^;
    {text:result.text, diagnostics:result.diagnostics}
}
[for (source in cases) run(source)^];
let malformed = [
    "\\makeatletter\\@for x:=a\\do{Y}END\\makeatother",
    "\\makeatletter\\@for\\x?=a\\do{Y}END\\makeatother",
    "\\makeatletter\\@for\\x:=a,b\\makeatother"
];
fn run_malformed(source) any^ {
    let result = parse(source, "tex")^;
    {text:result.text, diagnostics:[for (diagnostic in result.diagnostics) diagnostic.code]}
}
[for (source in malformed) run_malformed(source)^]

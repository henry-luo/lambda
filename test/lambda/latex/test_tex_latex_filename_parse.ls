// S1.8, D10.2.2v2: filename parsing is a scoped TeX definition operation.
let cases=[
    "\\makeatletter\\filename@parse{a/b/file.sty}[\\filename@area][\\filename@base][\\filename@ext]\\makeatother",
    "\\makeatletter\\filename@parse{dir/name}[\\filename@area][\\filename@base]\\ifx\\filename@ext\\relax[none]\\else[bad]\\fi\\makeatother",
    "\\makeatletter\\filename@parse{.hidden}[\\filename@area][\\filename@base][\\filename@ext]\\makeatother",
    "\\makeatletter\\filename@parse{a/b.}[\\filename@area][\\filename@base][\\filename@ext]\\makeatother",
    "\\makeatletter\\def\\path{dir/one.two.tex}\\filename@parse{\\path}[\\filename@area][\\filename@base][\\filename@ext]\\makeatother",
    "\\makeatletter\\filename@parse{outer.sty}{\\filename@parse{inner.tex}[\\filename@base]}[\\filename@base]\\makeatother",
    "\\makeatletter\\filename@parse{file.tex}\\def\\a{file}\\ifx\\filename@base\\a Y\\else N\\fi\\makeatother",
    "\\makeatletter\\catcode`\\z=12 \\filename@parse{zile.tex}\\def\\a{zile}\\ifx\\filename@base\\a Y\\else N\\fi\\makeatother",
    "\\makeatletter\\filename@parse{}[\\filename@area][\\filename@base]\\ifx\\filename@ext\\relax[none]\\fi\\makeatother"
];
fn run(source) any^ {
    let result=parse(source,"tex")^;
    {text:result.text, diagnostics:[for (diagnostic in result.diagnostics) diagnostic.code]}
}
[for (source in cases) run(source)^]

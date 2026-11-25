let base = "test/lambda/latex/fixtures/script_public_include.tex";
let sources = ["\\input{definitely-absent}", "first\n\\input{definitely-absent}",
    "\\input{../outside}", "\\input{definitely-absent.tex}After"];
[for (source in sources) {
    let result = parse(source,{type:"tex",base:base})^;
    {text:result.text, diagnostics:result.diagnostics, messages:len(result.messages)}
}]

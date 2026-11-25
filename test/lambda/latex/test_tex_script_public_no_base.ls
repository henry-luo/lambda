let sources = ["\\input{somefile}", "\\input{../outside}"];
[for (source in sources) {
    let result = parse(source,{type:"tex"})^;
    {text:result.text, diagnostics:result.diagnostics}
}]

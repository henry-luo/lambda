let source = "\\ExplSyntaxOn\\cs_new:Npn\\double:n#1{#1#1}\\double:n{A}\\ExplSyntaxOff";
let result = parse(source,{type:"tex"})^;
{text:result.text, messages:result.messages, diagnostics:result.diagnostics, packages:result.packages}

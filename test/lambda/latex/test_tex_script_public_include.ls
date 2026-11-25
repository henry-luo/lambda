let root = "test/lambda/latex/fixtures/";
let source = input(root ++ "script_public_include.tex", "text")^;
let from_text = parse(source, {type:"tex",base:root ++ "script_public_include.tex"})^;
let from_file = input(root ++ "script_public_include.tex", "tex")^;
{matches:from_text == from_file, text:from_file.text, diagnostics:from_file.diagnostics,
    messages:len(from_file.messages), packages:from_file.packages}

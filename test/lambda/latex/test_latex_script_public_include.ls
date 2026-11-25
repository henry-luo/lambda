let root = "test/lambda/latex/fixtures/";
let source = input(root ++ "script_public_latex_include.tex", "text")^;
let from_text = parse(source, {type:"latex",base:root ++ "script_public_latex_include.tex"})^;
let from_file = input(root ++ "script_public_latex_include.tex", "latex")^;
{matches:from_text == from_file, document:from_file}

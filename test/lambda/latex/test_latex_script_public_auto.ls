let path = "test/lambda/latex/fixtures/script_public_latex_include.tex";
let automatic = input(path)^;
let named = input(path, "auto")^;
let mapped = input(path, {type:"auto"})^;
let explicit = input(path, "latex")^;
{matches:automatic == explicit and named == explicit and mapped == explicit, document:automatic}

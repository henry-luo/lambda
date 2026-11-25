let path = \.test.lambda.latex.fixtures["script_public_latex_include.tex"];
let from_path = input(path, "latex")^;
let from_string = input("test/lambda/latex/fixtures/script_public_latex_include.tex", "latex")^;
{matches:from_path == from_string}

// File acquisition and parsing must use the same Script path as parse().
let source = input("test/lambda/latex/fixtures/script_public_input.tex", "text")^;
let from_file = input("test/lambda/latex/fixtures/script_public_input.tex", "tex")^;
let from_text = parse(source, {type:"tex"})^;
let oracle = input("test/lambda/latex/fixtures/tex_native_oracle.json", "json")^;
{matches:from_file == from_text and format(from_file, 'json')^ == oracle[13].expected}

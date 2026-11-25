let path = "test/lambda/latex/fixtures/script_public_jobname.tex";
let source = input(path, "text")^;
let from_text = parse(source, {type:"tex",base:path})^;
let from_file = input(path, "tex")^;
{matches:from_text == from_file, text:from_file.text}

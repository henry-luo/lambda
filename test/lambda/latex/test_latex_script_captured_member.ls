let archive = input("test/lambda/latex/fixtures/script_captured_member.zip")^;
let member = content(archive)^[0];
let tex = input(member, "tex")^;
let parsed = parse(member, {type:"tex"})^;
let automatic = input(member)^;
let latex = input(member, "latex")^;
{tex_matches:tex == parsed and input(member, "tex")^ == tex,
    text:tex.text, latex_matches:automatic == latex, document:automatic, latex:latex}

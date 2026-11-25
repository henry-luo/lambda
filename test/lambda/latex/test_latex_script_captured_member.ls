let archive = input("test/lambda/latex/fixtures/script_captured_member.zip")^;
let member = content(archive)^[0];
let tex = input(member, "tex")^;
let parsed = parse(member, {type:"tex"})^;
let automatic = input(member)^;
let latex = input(member, "latex")^;
let lazy = content(member)^;
let graph = [for (entry in content(archive)^ where entry.name == "graph.dot") entry][0];
let graph_content = content(graph)^;
{tex_matches:tex == parsed and input(member, "tex")^ == tex,
    text:tex.text, latex_matches:automatic == latex,
    lazy_matches:len(lazy) == 1 and lazy[0] == latex and member[0] == latex,
    graph_matches:len(graph_content) == 1 and graph_content[0] is <graph>,
    document:automatic, latex:latex}

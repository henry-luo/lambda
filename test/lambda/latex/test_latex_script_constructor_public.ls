// D10.2.2v2: document constructors retain assignments in their arguments.
let source = "\\count0=0\\begin{document}\\textbf{\\global\\count0=7 X}\\the\\count0\\end{document}";
let expected = <latex_document <document source_offset:9,
    <paragraph <textbf source_offset:25, <global source_offset:33, argument_groups:[]>
        <count source_offset:40, argument_groups:[]> "0=7 X"> "0">>>;
let tree = parse(source,"latex")^;
let expl = "\\ExplSyntaxOn\\begin{document}\\textbf{\\tl_set:Nx\\l_tmpa_tl{A}B}\\end{document}";
let explicit = parse(source,{type:"latex",constructor_argument_mode:true})^;
let legacy = parse(source,{type:"latex",constructor_argument_mode:false})^;
[
    tree == expected,
    explicit == expected,
    tree != legacy,
    parse(expl,"latex")^ is <latex_document>,
    parse(expl,"tex")^.text == "\\begin{document}\\textbf{\\cs_set_nopar:Npx{\\tl_set:Nn\\l_tmpa_tl{A}}B}\\end{document}"
]

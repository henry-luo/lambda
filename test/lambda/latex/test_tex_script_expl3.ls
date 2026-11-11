import profile: lambda.doc.latex.tex_engine.profile
let kernel = input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
fn report(source) any^ {
    let result = profile.expand_with_kernel(source, kernel, {script_expl3: true, echo_definitions: true}, 100000)^;
    let text = join([for (token in result.output) token.value], "");
    {text: text, issue: result.issue, unsupported: result.unsupported,
        matches_native: if (result.issue != null) null else text == (parse(source, {type: "tex"})^.text)}
}
"function:"; report("\\ExplSyntaxOn\\cs_new:Npn\\double:n#1{#1#1}\\double:n{A}\\ExplSyntaxOff")
"two parameters:"; report("\\ExplSyntaxOn\\cs_new:Npn\\pair:nn#1#2{#1/#2}\\pair:nn{A}{B}\\ExplSyntaxOff")
"token list:"; report("\\ExplSyntaxOn\\tl_new:N\\l_test_tl\\tl_set:Nn\\l_test_tl{Hello~world}\\tl_use:N\\l_test_tl\\ExplSyntaxOff")
"expanded token list:"; report("\\def\\word{A}\\ExplSyntaxOn\\tl_new:N\\l_test_tl\\tl_set:Nx\\l_test_tl{\\word}\\def\\word{B}\\tl_use:N\\l_test_tl\\ExplSyntaxOff")
"unexpanded token list:"; report("\\def\\word{A}\\ExplSyntaxOn\\tl_new:N\\l_test_tl\\tl_set:Nn\\l_test_tl{\\word}\\def\\word{B}\\tl_use:N\\l_test_tl\\ExplSyntaxOff")
"local assignment:"; report("\\ExplSyntaxOn\\tl_new:N\\l_test_tl\\tl_set:Nn\\l_test_tl{A}{\\tl_set:Nn\\l_test_tl{B}\\tl_use:N\\l_test_tl}\\tl_use:N\\l_test_tl\\ExplSyntaxOff")
"global assignment:"; report("\\ExplSyntaxOn\\tl_new:N\\l_test_tl\\tl_set:Nn\\l_test_tl{A}{\\tl_gset:Nn\\l_test_tl{B}}\\tl_use:N\\l_test_tl\\ExplSyntaxOff")
"constant:"; report("\\ExplSyntaxOn\\tl_const:Nn\\c_mark_tl{ready~now}\\tl_use:N\\c_mark_tl\\ExplSyntaxOff")
"clear:"; report("\\ExplSyntaxOn\\tl_new:N\\l_test_tl\\tl_set:Nn\\l_test_tl{A}\\tl_clear:N\\l_test_tl[\\tl_use:N\\l_test_tl]\\ExplSyntaxOff")
"global declaration:"; report("\\ExplSyntaxOn{\\cs_new:Npn\\test:{Y}}\\test:\\ExplSyntaxOff")
"syntax restoration:"; report("\\ExplSyntaxOn\\ExplSyntaxOn\\ExplSyntaxOff\\number\\catcode95/\\number\\catcode58/\\number\\catcode32")
"syntax scoped:"; report("{\\ExplSyntaxOn\\number\\catcode95}\\number\\catcode95")
"syntax nested restore:"; report("\\ExplSyntaxOn{\\ExplSyntaxOff\\number\\catcode95}\\number\\catcode95\\ExplSyntaxOff\\number\\catcode95")
"expanded use:"; report("\\ExplSyntaxOn\\tl_new:N\\l_test_tl\\tl_set:Nn\\l_test_tl{A}\\edef\\x{\\tl_use:N\\l_test_tl}\\x\\ExplSyntaxOff")
"protected function:"; report("\\ExplSyntaxOn\\cs_new_protected:Npn\\safe:{A}\\edef\\x{\\safe:}\\x\\ExplSyntaxOff")
"unsupported operation:"; report("\\ExplSyntaxOn\\seq_pop_left:NN\\l_test_seq\\l_tmpa_tl")
"duplicate declaration:"; report("\\ExplSyntaxOn\\tl_new:N\\l_test_tl\\tl_new:N\\l_test_tl")
"undefined token list:"; report("\\ExplSyntaxOn\\tl_use:N\\l_missing_tl")
"custom syntax restoration:"; report("\\catcode95=12 \\ExplSyntaxOn\\ExplSyntaxOff\\number\\catcode95")
"local function:"; report("\\ExplSyntaxOn\\cs_new:Npn\\value:{A}{\\cs_set:Npn\\value:{B}\\value:}\\value:\\ExplSyntaxOff")
"global clear:"; report("\\ExplSyntaxOn\\tl_new:N\\l_test_tl\\tl_set:Nn\\l_test_tl{A}{\\tl_gclear:N\\l_test_tl}[\\tl_use:N\\l_test_tl]\\ExplSyntaxOff")
"noexpand with token use:"; report("\\ExplSyntaxOn\\tl_new:N\\l_test_tl\\tl_set:Nn\\l_test_tl{A}\\edef\\x{\\noexpand\\tl_use:N\\noexpand\\l_test_tl}\\x\\ExplSyntaxOff")
"integer expression:"; report("\\ExplSyntaxOn\\int_eval:n{1+2} / \\int_eval:n{7/2} / \\int_eval:n{(2+3)*4}\\ExplSyntaxOff")
"global syntax setting:"; report("\\globaldefs=1 {\\ExplSyntaxOn}\\number\\catcode95\\ExplSyntaxOff\\number\\catcode95\\globaldefs=0")
fn report_package(source, resources = {local: {}, bundled: {}}) any^ {
    let result = profile.expand_with_kernel(source, kernel, {script_expl3: true, resources: resources}, 100000)^;
    {text: join([for (token in result.output) token.value], ""), issue: result.issue,
        unsupported: result.unsupported, packages: [for (entry in result.packages) [entry.name, entry.kind]]}
}
let sample_style = "\\ProvidesExplPackage{sample}{2026-11-11}{1.0}{test}\\cs_new:Npn\\demo:n#1{[#1]}\\tl_const:Nn\\c_mark_tl{OK}";
"script expl3 package:"; report_package("\\usepackage{expl3}\\ExplSyntaxOn\\tl_new:N\\l_test_tl\\tl_set:Nn\\l_test_tl{A}\\tl_use:N\\l_test_tl\\ExplSyntaxOff")
"script expl3 duplicate:"; report_package("\\usepackage{expl3}\\usepackage{expl3}X")
"script expl3 package option:"; report_package("\\usepackage[unknown]{expl3}")
"local expl3 package:"; report_package("\\usepackage{sample}\\ExplSyntaxOn\\demo:n{Q}/\\tl_use:N\\c_mark_tl\\ExplSyntaxOff/\\number\\catcode95",
    {local: {'sample.sty': {data: sample_style}}, bundled: {}})
"package keeps enclosing syntax:"; report_package("\\ExplSyntaxOn\\usepackage{sample}\\number\\catcode95/\\demo:n{Q}\\ExplSyntaxOff\\number\\catcode95",
    {local: {'sample.sty': {data: sample_style}}, bundled: {}})
"nested expl3 packages:"; report_package("\\usepackage{outer}\\ExplSyntaxOn\\outer:/\\inner:\\ExplSyntaxOff/\\number\\catcode95",
    {local: {'outer.sty': {data: "\\ProvidesExplPackage{outer}{2026}{1}{outer}\\RequirePackage{inner}\\cs_new:Npn\\outer:{O}"},
        'inner.sty': {data: "\\ProvidesExplPackage{inner}{2026}{1}{inner}\\cs_new:Npn\\inner:{I}"}}, bundled: {}})
"unsupported package operation:"; report_package("\\usepackage{unsupported}",
    {local: {'unsupported.sty': {data: "\\ProvidesExplPackage{unsupported}{2026}{1}{test}\\seq_pop_left:NN\\l_test_seq\\l_tmpa_tl"}}, bundled: {}})
"document command mandatory:"; report("\\NewDocumentCommand\\foo{m}{[#1]}\\foo{A}")
"document command optional:"; report("\\NewDocumentCommand\\foo{O{Z}}{[#1]}\\foo/\\foo[X]")
"document command two arguments:"; report("\\NewDocumentCommand\\foo{m m}{#1/#2}\\foo{A}{B}")
"document command mixed:"; report("\\NewDocumentCommand\\foo{O{D} m}{#1/#2}\\foo{X}/\\foo[Q]{Y}")
"document command scoped:"; report("{\\NewDocumentCommand\\foo{m}{[#1]}\\foo{X}}\\ifdefined\\foo BAD\\else OK\\fi")
"document command unsupported spec:"; report("\\NewDocumentCommand\\foo{s}{#1}\\foo*")
"expl3 package document commands:"; report_package("\\usepackage{sample}\\greet/\\greet[Friend]/\\twice{A}",
    {local: {'sample.sty': {data: "\\ProvidesExplPackage{sample}{2026}{1}{test}\\cs_new:Npn\\double:n#1{#1#1}\\NewDocumentCommand\\greet{O{World}}{Hello,~#1!}\\NewDocumentCommand\\twice{m}{\\double:n{#1}}"}}, bundled: {}})
let fixture_style = input("test/lambda/latex/fixtures/local_packages/lmdexpl.sty", "text")^;
"project expl3 fixture loads:"; report_package("\\usepackage{lmdexpl}",
    {local: {'lmdexpl.sty': {data: fixture_style}}, bundled: {}})
"integer with macro arguments:"; report("\\ExplSyntaxOn\\cs_new:Npn\\sum:nn#1#2{\\int_eval:n{#1+#2}}\\sum:nn{12}{-3}\\ExplSyntaxOff")
"integer expression error:"; report("\\ExplSyntaxOn\\int_eval:n{1/0}")

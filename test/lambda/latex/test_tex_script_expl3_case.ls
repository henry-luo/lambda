import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel = input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
fn report(source) any^ {
    let result = profile.expand_with_kernel(source, kernel, {script_expl3: true, echo_definitions: true}, 100000)^;
    let text = join([for (token in result.output) if (token.kind == "control") "\\" ++ token.value else token.value], "");
    {text: text, issue: result.issue, unsupported: result.unsupported,
        matches_native: if (result.issue != null) null else text == (parse(source, {type: "tex"})^.text)}
}
"uppercase:"; report("\\ExplSyntaxOn\\text_uppercase:n{Hello,~world!}\\ExplSyntaxOff")
"lowercase:"; report("\\ExplSyntaxOn\\text_lowercase:n{HELLO,~WORLD!}\\ExplSyntaxOff")
"macro expansion:"; report("\\ExplSyntaxOn\\cs_new:Npn\\word:{mixed}\\text_uppercase:n{pre~\\word:~post}\\ExplSyntaxOff")
"formatting:"; report("\\ExplSyntaxOn\\text_uppercase:n{a~\\textbf{bold}~z}\\ExplSyntaxOff")
"protected function:"; report("\\ExplSyntaxOn\\cs_new_protected:Npn\\box:n#1{[#1]}\\text_uppercase:n{a\\box:n{word}z}\\ExplSyntaxOff")
"groups:"; report("\\ExplSyntaxOn\\text_uppercase:n{a{b}c}\\ExplSyntaxOff")
"reference key:"; report("\\ExplSyntaxOn\\text_uppercase:n{a~\\ref{label}~b}\\ExplSyntaxOff")
"math is preserved:"; report("\\ExplSyntaxOn\\text_uppercase:n{a~$x^2$~b}\\ExplSyntaxOff")
"parenthesized math:"; report("\\ExplSyntaxOn\\text_uppercase:n{a~\\(x+y\\)~z}\\ExplSyntaxOff")
"expanded definition preserves math macro:"; report("\\ExplSyntaxOn\\cs_new:Npn\\word:{abc}\\edef\\saved{\\text_uppercase:n{a$\\word:$b}}\\cs_set:Npn\\word:{z}\\saved\\ExplSyntaxOff")
"sequence input:"; report("\\ExplSyntaxOn\\seq_new:N\\l_test_seq\\seq_set_from_clist:Nn\\l_test_seq{abc,def}\\text_uppercase:n{\\seq_use:Nn\\l_test_seq{,}}\\ExplSyntaxOff")
"unknown control:"; report("\\ExplSyntaxOn\\text_uppercase:n{a\\unknown{b}}\\ExplSyntaxOff")
"unicode pending:"; report("\\ExplSyntaxOn\\text_uppercase:n{Straße}\\ExplSyntaxOff")
"reference options pending:"; report("\\ExplSyntaxOn\\text_uppercase:n{\\cite[pre]{key}}\\ExplSyntaxOff")
let fixture_style = input("test/lambda/latex/fixtures/local_packages/lmdexpl.sty", "text")^;
let fixture_source = "\\usepackage{lmdexpl}\\greet/\\twice{A}/\\sumof{8}{5}/\\sorted{pear,apple,kiwi}/\\shout{Hello, world}/\\ready";
let fixture = profile.expand_with_kernel(fixture_source, kernel, {script_expl3: true,
    resources: {local: {'lmdexpl.sty': {data: fixture_style}}, bundled: {}}}, 100000)^;
"project fixture case conversion:"; {text: join([for (token in fixture.output) token.value], ""),
    issue: fixture.issue, unsupported: fixture.unsupported}
"reference star pending:"; report("\\ExplSyntaxOn\\text_uppercase:n{\\ref*{key}}\\ExplSyntaxOff")
let latex_source = "\\begin{document}\\ExplSyntaxOn\\text_uppercase:n{Hello~world}\\ExplSyntaxOff\\end{document}";
let expanded = profile.expand_with_kernel(latex_source, kernel, {script_expl3: true, echo_definitions: true}, 100000)^;
let ast = digest.digest(expanded)^;
"case conversion AST:"; {issue: ast.issue, matches_native: if (ast.ast == null) null else ast.ast == (parse(latex_source, {type:"latex"})^)}

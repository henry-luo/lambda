import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
import native_snapshot: ._tex_native_snapshot
let kernel=input("lmd/package/doc/latex/tex_engine/kernel.tex","text")^;
// The production input path is Script; preserve the retired native trees independently.
let native=input("test/lambda/latex/fixtures/tex_paren_reconstruction_native.json","json")^;
fn check(case, reference) any^ {
    let expanded=profile.expand_with_kernel(case.source,kernel,
        {echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
    let actual=digest.digest(expanded)^;
    let same=if(actual.issue==null) native_snapshot.tree_matches(actual.ast,reference,case.source)^
        else case.source==reference.source;
    let passes = if(actual.issue==null) same
        else same and case.guard!=null and actual.issue=="latex-digest-unsupported" and actual.unsupported==case.guard;
    {source:case.source,passed:passes,issue:actual.issue,unsupported:actual.unsupported,
        actual:if(actual.issue==null and not same)actual.ast else null,
        native:if(actual.issue==null and not same)reference.latex else null}
}
let cases=[
 {source:"\\def\\a{(1,2)}\\begin{document}\\mystery\\a\\end{document}"},
 {source:"\\def\\a{1,2}\\begin{document}\\mystery(\\a)\\end{document}"},
 {source:"\\def\\a{1}\\def\\b{2}\\begin{document}\\mystery(\\a,\\b)\\end{document}"},
 {source:"\\def\\a{(1,2)}\\begin{document}\\mystery\\a{X}\\end{document}"},
 {source:"\\def\\a{(1,2)}\\begin{document}\\mystery\\a[Q]{X}\\end{document}"},
 {source:"\\def\\a{(-1.25,+2)}\\begin{document}\\mystery\\a!\\end{document}"},
 {source:"é\\def\\a{(1,2)}\\begin{document}\\mystery\\a\\end{document}"},
 {source:"\\def\\a{1,2}\\begin{document}\\mystery( \\a )\\end{document}"},
 {source:"\\def\\a{(abc)}\\begin{document}\\mystery\\a\\end{document}"},
 {source:"\\def\\a{(abc)}\\begin{document}\\mystery\\a{X}\\end{document}"},
 {source:"\\def\\a{(1,2)}\\begin{document}\\textbf{\\mystery\\a}\\end{document}"},
 {source:"\\def\\a{(1,2)}\\begin{document}\\mystery\\a\\mystery\\a\\end{document}"},
 {source:"\\def\\a{(1,2)}\\begin{document}\\mystery*\\a{X}\\end{document}"},
 {source:"\\def\\a{(1,2)}\\begin{document}\\mystery[Q]\\a\\end{document}"},
 {source:"\\def\\a{(1,2)}\\begin{document}\\mystery{Q}\\a\\end{document}"},
 {source:"\\def\\a{1}\\begin{document}\\mystery(\\a,\n2)\\end{document}"},
 {source:"\\def\\a{(1,2)}\\mystery\\a"},
 {source:"\\def\\a{1,2}\\mystery(\\a)"},
 {source:"\\def\\a{(1,2}\\begin{document}\\mystery\\a\\end{document}",guard:"parenthesized command source mapping"},
 {source:"\\def\\a{(1,2)}\\def\\m{\\mystery}\\begin{document}\\m\\a\\end{document}",guard:"command source mapping"}
];
let results=[for(index,case in cases) check(case,native.cases[index])^];
{cases:len(results),failures:[for(result in results where not result.passed) result]}

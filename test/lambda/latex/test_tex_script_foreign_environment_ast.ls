import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let source="\\input{nested-env-child.tex}\\begin{document}\\begin{sample}Hello\\end{sample}\\end{document}";
let options={echo_definitions:true,constructor_argument_mode:true,adapters:[]};
let cases=[{child:"\\newenvironment{sample}\n{before}\n{after}\n",expected:<latex_document <paragraph "\n"> <document source_offset: 28, <paragraph "beforeHelloafter">>>},
{child:"\\newenvironment{sample}{before}{after}\n",expected:<latex_document <paragraph "\n"> <document source_offset: 28, <paragraph "beforeHelloafter">>>},
{child:"\\newenvironment{sample}\n\n{before}\n{after}\n",expected:<latex_document <paragraph "\n" <curly_group "after"> "\n"> <document source_offset: 28, <paragraph "Hellobefore">>>},
{child:"\\newenvironment{sample}\n{before}\n\n{after}\n",expected:<latex_document <paragraph <curly_group "after"> "\n"> <document source_offset: 28, <paragraph "beforeHello">>>},
{child:"\\newenvironment{sample}\n{before}\n{after}\n\n",expected:<latex_document <document source_offset: 28, <paragraph "beforeHelloafter">>>},
{child:"\\newenvironment{sample}\n{before}\n{after}\n\n\n",expected:<latex_document <paragraph "\n"> <document source_offset: 28, <paragraph "beforeHelloafter">>>},
{child:"\\newenvironment{sample}\n{before}\n{after}\n% note\n",expected:<latex_document <paragraph "\n"> <document source_offset: 28, <paragraph "beforeHelloafter">>>},
{child:"\\newenvironment{sample}\n{before}\n{after}\n\nText\n",expected:<latex_document <paragraph "Text\n"> <document source_offset: 28, <paragraph "beforeHelloafter">>>},
{child:"\\newenvironment{sample}\r\n{before}\r\n{after}\r\n",expected:<latex_document <paragraph "\r\n"> <document source_offset: 28, <paragraph "beforeHelloafter">>>},
{child:"\\newenvironment{sample}\n{before}\n{after}\n\\def\\extra{Y}\n",expected:<latex_document <document source_offset: 28, <paragraph "beforeHelloafter">>>},
{child:"\\def\\extra{Y}\n\\newenvironment{sample}\n{before}\n{after}\n",expected:<latex_document <document source_offset: 28, <paragraph "beforeHelloafter">>>},
{child:"\\newenvironment{sample}\n{before}\n{after}\n\\newcommand{\\x}{X}\n",expected:<latex_document <document source_offset: 28, <paragraph "beforeHelloafter">>>}];
// Complete trees were captured from the retired native engines.
[for (c in cases) (let configured={*:options,resources:{local:{'nested-env-child.tex':{data:c.child}},bundled:{}}},
    let r=profile.expand_with_kernel(source,kernel,configured,100000)^,
    let d=digest.digest(r)^, {issue:d.issue, matches:d.ast==c.expected})]

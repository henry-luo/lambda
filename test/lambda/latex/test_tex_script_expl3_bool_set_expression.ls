// D10.2.2v2: Boolean expression assignment and scope are compared with TeX Live 2025.
import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let options={script_expl3:true};
let preload=profile.preload_expl3_kernel(kernel,options,100000)^;
let prefix="\\ExplSyntaxOn\\bool_new:N\\l_test_bool";
let report="\\bool_if:NTF\\l_test_bool{T}{F}";
let cases=[
 {name:"true",body:"\\bool_set:Nn\\l_test_bool{\\c_true_bool}" ++ report,expected:"T"},
 {name:"false",body:"\\bool_set:Nn\\l_test_bool{\\c_false_bool}" ++ report,expected:"F"},
 {name:"expression",body:"\\bool_set:Nn\\l_test_bool{!\\c_false_bool&&(\\c_true_bool||\\c_false_bool)}" ++ report,expected:"T"},
 {name:"local",body:"\\bool_set:Nn\\l_test_bool{\\c_false_bool}{\\bool_set:Nn\\l_test_bool{\\c_true_bool}" ++ report ++ "}" ++ report,expected:"{T}F"},
 {name:"global",body:"\\bool_set:Nn\\l_test_bool{\\c_false_bool}{\\bool_gset:Nn\\l_test_bool{\\c_true_bool}" ++ report ++ "}" ++ report,expected:"{T}T"},
 {name:"globaldefs-positive",body:"\\bool_set:Nn\\l_test_bool{\\c_false_bool}{\\globaldefs=1\\bool_set:Nn\\l_test_bool{\\c_true_bool}}" ++ report,expected:"{}T"},
 {name:"globaldefs-negative",body:"\\bool_set:Nn\\l_test_bool{\\c_false_bool}{\\globaldefs=-1\\bool_gset:Nn\\l_test_bool{\\c_true_bool}}" ++ report,expected:"{}F"},
 {name:"predicate",body:"\\bool_set:Nn\\l_test_bool{\\int_compare_p:nNn{3}>{2}}" ++ report,expected:"T"},
 {name:"string-predicate-true",body:"\\bool_set:Nn\\l_test_bool{\\str_if_eq_p:nn{F}{F}}" ++ report,expected:"T"},
 {name:"string-predicate-false",body:"\\bool_set:Nn\\l_test_bool{\\str_if_eq_p:nn{F}{T}}" ++ report,expected:"F"},
 {name:"predicate-token-true",body:"\\edef\\x{\\str_if_eq_p:nn{a}{a}}\\meaning\\x",expected:"macro:->\\c_true_bool "},
 {name:"predicate-token-false",body:"\\edef\\x{\\str_if_eq_p:nn{a}{b}}\\meaning\\x",expected:"macro:->\\c_false_bool "},
 {name:"predicate-eof",body:"\\ior_new:N\\g_test_ior\\edef\\x{\\ior_if_eof_p:N\\g_test_ior}\\meaning\\x",expected:"macro:->\\c_true_bool "},
 {name:"predicate-expandable",body:"\\edef\\x{\\token_if_expandable_p:N\\tl_to_str:n}\\meaning\\x",expected:"macro:->\\c_true_bool "},
 {name:"after-assignment",body:"\\def\\marker{A}\\afterassignment\\marker\\bool_set:Nn\\l_test_bool{\\c_true_bool}" ++ report,expected:"AT"},
 {name:"meaning",body:"\\meaning\\bool_set:Nn",expected:"\\protected\\long macro:#1#2->\\exp_last_unbraced:NNNf \\tex_chardef:D #1={\\bool_if_p:n {#2}}"}];
[for(c in cases) (let r=profile.expand_with_kernel(prefix ++ c.body,kernel,options,100000,preload)^,
 let text=join([for(t in r.output)t.value],""),
 {name:c.name,issue:r.issue,unsupported:r.unsupported,diagnostics:r.diagnostic_records,text:text,matches:text==c.expected and len(r.diagnostic_records)==0})]
let doc_options={*:options,constructor_argument_mode:true};
let doc_preload=profile.preload_expl3_kernel(kernel,doc_options,100000)^;
let doc=profile.expand_with_kernel(prefix ++ cases[0].body,kernel,doc_options,100000,doc_preload)^;
let tree=digest.digest(doc)^;
{issue:tree.issue,unsupported:tree.unsupported}
let nested=profile.expand_with_kernel(prefix ++ "\\begin{document}\\textbf{\\bool_set:Nn\\l_test_bool{\\c_true_bool}X}\\end{document}",kernel,doc_options,100000,doc_preload)^;
{issue:nested.issue,unsupported:nested.unsupported}

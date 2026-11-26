import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let options={script_expl3:true};
let preload=profile.preload_expl3_kernel(kernel,options,100000)^;
let prefix="\\ExplSyntaxOn\\def\\foo{hello}\\def\\bar{hello}\\def\\baz{world}";
let cases=[
 {name:"expanded-left",body:"\\str_if_eq:eeTF{\\foo}{hello}{T}{F}",expected:"T"},
 {name:"expanded-both",body:"\\str_if_eq:eeTF{\\foo}{\\bar}{T}{F}",expected:"T"},
 {name:"expanded-different",body:"\\str_if_eq:eeTF{\\foo}{\\baz}{T}{F}",expected:"F"},
 {name:"empty",body:"\\str_if_eq:eeTF{}{}{T}{F}",expected:"T"},
 {name:"spaces",body:"\\str_if_eq:eeTF{a~b}{a~b}{T}{F}",expected:"T"},
 {name:"true",body:"\\str_if_eq:eeT{\\foo}{hello}{T}",expected:"T"},
 {name:"false",body:"\\str_if_eq:eeF{\\foo}{world}{T}",expected:"T"},
 {name:"predicate-equal",body:"\\bool_if:nTF{\\str_if_eq_p:ee{\\foo}{\\bar}}{T}{F}",expected:"T"},
 {name:"predicate-different",body:"\\bool_if:nTF{\\str_if_eq_p:ee{\\foo}{\\baz}}{T}{F}",expected:"F"},
 {name:"meaning",body:"\\meaning\\str_if_eq:eeTF",expected:"\\long macro:#1#2->\\if:w 0\\__str_if_eq:nn {#1}{#2}\\__prg_TF_true:w \\fi: \\use_ii:nn "}];
[for(c in cases) (let r=profile.expand_with_kernel(prefix ++ c.body,kernel,options,100000,preload)^,
 let text=join([for(t in r.output)t.value],""),
 {name:c.name,issue:r.issue,unsupported:r.unsupported,text:text,matches:text==c.expected})]
let document_options={*:options,constructor_argument_mode:true};
let document_preload=profile.preload_expl3_kernel(kernel,document_options,100000)^;
let document=profile.expand_with_kernel(prefix ++ cases[0].body,kernel,document_options,100000,document_preload)^;
let tree=digest.digest(document)^;
{issue:tree.issue,unsupported:tree.unsupported}

import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let options={script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}};
let constructor={*:options,constructor_argument_mode:true};
let legacy_preload=profile.preload_expl3_kernel(kernel,options,100000)^;
let constructor_preload=profile.preload_expl3_kernel(kernel,constructor,100000)^;
let sources = ["\\ExplSyntaxOn\\font\\f=cmr10~at~1sp\\relax\\the\\fontdimen9\\f\\relax\\ExplSyntaxOff", "\\ExplSyntaxOn\\font\\f=cmr10~at~1sp\\relax\\fontdimen9\\f=3sp\\relax\\ExplSyntaxOff", "\\ExplSyntaxOn\\font\\f=cmr10~at~1sp\\relax\\the\\hyphenchar\\f\\relax\\ExplSyntaxOff", "\\ExplSyntaxOn\\font\\f=cmr10~at~1sp\\relax\\hyphenchar\\f=2\\relax\\ExplSyntaxOff", "\\ExplSyntaxOn\\the\\fontdimen8\\nullfont\\relax\\ExplSyntaxOff", "\\ExplSyntaxOn\\font\\f=cmr10~at~1sp\\relax\\ExplSyntaxOff\\the\\fontdimen9\\f\\relax"];
// Initialized arrays can be read; mutation and unknown font state remain guarded.
[for (entry in [{options:options,preload:legacy_preload},{options:constructor,preload:constructor_preload}])
 [for (source in sources) (let r=profile.expand_with_kernel(source,kernel,entry.options,100000,entry.preload)^,
   {issue:r.issue,unsupported:r.unsupported})]];
[for (source in sources) parse(source,{type:"tex"}) ^ {null}]

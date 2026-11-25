// D10.2.2v2: key-value parsing retains unexpanded tokens and TeX grouping.
import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let options={script_expl3:true};
let preload=profile.preload_expl3_kernel(kernel,options,100000)^;
let definitions="\\ExplSyntaxOn\\cs_new:Npn\\capture:n#1{N[\\tl_to_str:n{#1}]}" ++
    "\\cs_new:Npn\\capture:nn#1#2{V[\\tl_to_str:n{#1}]=[\\tl_to_str:n{#2}]}\\def\\unexpandedword{EXPANDED}";
fn report(source) any^ {
    let r=profile.expand_with_kernel(definitions ++ source,kernel,options,100000,preload)^;
    {issue:r.issue,unsupported:r.unsupported,text:join([for(t in r.output)t.value],"")}
}
// The corresponding expandable results were compared with TeX Live.
report("\\edef\\result{\\keyval_parse:NNn\\capture:n\\capture:nn{a=1,~b={one,two},~c,~empty=,~{a,b}=v,~{q},~{{g}}={{v}}}}\\result")^;
report("\\edef\\result{\\keyval_parse:nnn{\\capture:n}{\\capture:nn}{~,~{a=b}={c=d},~raw={\\unexpandedword},,,~tail~}}\\result")^;
report("\\edef\\result{\\keyval_parse:NNn\\capture:n\\capture:nn{first={x,{y,z}},second={v=w}}}\\result")^;
report("\\catcode44=13\\relax\\catcode61=13\\relax\\edef\\result{\\keyval_parse:NNn\\capture:n\\capture:nn{a=1,b={c,d},e}}\\result")^;
report("\\keyval_parse:NNn\\capture:n\\capture:nn{bad=x=y}")^;
report("\\keyval_parse:NNn\\capture:n\\capture:nn{=bad}")^;
let parsed=profile.expand_with_kernel(definitions ++ "\\keyval_parse:NNn\\capture:n\\capture:nn{a=1}",kernel,options,100000,preload)^;
let tree=digest.digest(parsed)^;
tree.ast == null and tree.issue == "latex-digest-unsupported" and tree.unsupported == "expl3 key-value source mapping"

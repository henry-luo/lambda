// D10.2.2v2: expected results captured from TeX Live 2025.
import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
import regex: lambda.doc.latex.tex_engine.regex
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let options={script_expl3:true};
let preload=profile.preload_expl3_kernel(kernel,options,100000)^;
let cases=[
 {name:"digits",pattern:"\\A\\d+\\Z",subject:"123",expected:"T"},
 {name:"empty",pattern:"\\A\\d+\\Z",subject:"",expected:"F"},
 {name:"prefix",pattern:"\\A\\d+\\Z",subject:"x123",expected:"F"},
 {name:"suffix",pattern:"\\A\\d+\\Z",subject:"123x",expected:"F"},
 {name:"search",pattern:"bc",subject:"abcd",expected:"T"},
 {name:"missing",pattern:"\\d+",subject:"abc",expected:"F"},
 {name:"star-empty",pattern:"\\Aa*\\Z",subject:"",expected:"T"},
 {name:"star-many",pattern:"\\Aa*\\Z",subject:"aaa",expected:"T"},
 {name:"plus-empty",pattern:"\\Aa+\\Z",subject:"",expected:"F"},
 {name:"plus-many",pattern:"\\Aa+\\Z",subject:"aaa",expected:"T"},
 {name:"question-empty",pattern:"\\Aa?\\Z",subject:"",expected:"T"},
 {name:"question-many",pattern:"\\Aa?\\Z",subject:"aa",expected:"F"},
 {name:"dot-one",pattern:"\\A.\\Z",subject:"x",expected:"T"},
 {name:"dot-empty",pattern:"\\A.\\Z",subject:"",expected:"F"},
 {name:"literal-dot",pattern:"\\A\\.\\Z",subject:".",expected:"T"},
 {name:"word",pattern:"\\A\\w+\\Z",subject:"a9_",expected:"T"},
 {name:"notword",pattern:"\\A\\W+\\Z",subject:".-",expected:"T"},
 {name:"space",pattern:"\\A\\s\\Z",subject:"~",expected:"T"},
 {name:"notspace",pattern:"\\A\\S\\Z",subject:"x",expected:"T"},
 {name:"pattern-space",pattern:"\\A a ~ + \\Z",subject:"aa",expected:"T"},
 {name:"class",pattern:"\\A[a-z0-9]+\\Z",subject:"a3z",expected:"T"},
 {name:"negclass",pattern:"\\A[^a-z]+\\Z",subject:"123",expected:"T"},
 {name:"control-digit",pattern:"\\A\\d\\Z",subject:"\\foo",expected:"F"},
 {name:"control-nondigit",pattern:"\\A\\D\\Z",subject:"\\foo",expected:"T"},
 {name:"control-word",pattern:"\\A\\w\\Z",subject:"\\foo",expected:"F"},
 {name:"control-nonword",pattern:"\\A\\W\\Z",subject:"\\foo",expected:"T"},
 {name:"control-space",pattern:"\\A\\s\\Z",subject:"\\foo",expected:"F"},
 {name:"control-nonspace",pattern:"\\A\\S\\Z",subject:"\\foo",expected:"T"},
 {name:"control-negclass",pattern:"\\A[^a]\\Z",subject:"\\foo",expected:"T"},
 {name:"control-class",pattern:"\\A[a-z]\\Z",subject:"\\foo",expected:"F"},
 {name:"control-dot",pattern:"\\A.\\Z",subject:"\\foo",expected:"T"},
 {name:"control-name",pattern:"\\A\\dfoo\\Z",subject:"1foo",expected:"T"},
 {name:"brace-one",pattern:"\\A.\\Z",subject:"{x}",expected:"F"},
 {name:"brace-three",pattern:"\\A...\\Z",subject:"{x}",expected:"T"},
 {name:"range-spaces",pattern:"\\A[a~-~z]\\Z",subject:"m",expected:"T"},
 {name:"control-search",pattern:"\\A\\d+\\Z",subject:"\\tl_trim_spaces:n{name}",expected:"F"}];
[for(c in cases) (let r=profile.expand_with_kernel("\\ExplSyntaxOn\\regex_match:nnTF{" ++ c.pattern ++ "}{" ++ c.subject ++ "}{T}{F}",kernel,options,100000,preload)^,
 let text=join([for(t in r.output)t.value],""),
 {name:c.name,issue:r.issue,unsupported:r.unsupported,text:text,matches:text==c.expected})]

// The T/F variants, control-sequence tokens, and space classes match TeX Live.
let commands=[
 {name:"true-variant",body:"\\regex_match:nnT{\\d}{4}{A}\\regex_match:nnT{\\d}{x}{B}",expected:"A"},
 {name:"false-variant",body:"\\regex_match:nnF{\\d}{x}{A}\\regex_match:nnF{\\d}{4}{B}",expected:"A"},
 {name:"nested",body:"\\regex_match:nnTF{a}{a}{\\regex_match:nnTF{b}{b}{A}{B}}{C}",expected:"A"},
 {name:"discarded-branch",body:"\\regex_match:nnTF{a}{a}{A}{\\undefined}",expected:"A"},
 {name:"meaning",body:"\\meaning\\regex_match:nnTF",expected:"\\protected\\long macro:#1#2->\\__regex_if_match:nn {\\__regex_build:n {#1}}{#2}\\__regex_return: \\exp_end: "}];
[for(c in commands) (let r=profile.expand_with_kernel("\\ExplSyntaxOn" ++ c.body,kernel,options,100000,preload)^,
 let text=join([for(t in r.output)t.value],""),
 {name:c.name,issue:r.issue,text:text,matches:text==c.expected})]
// Unported patterns, token categories, and assignment scopes must remain explicit.
let unsupported=[
 {name:"alternation",body:"\\regex_match:nnTF{a|b}{a}{A}{B}",expected:"expl3 regex pattern"},
 {name:"lazy-repeat",body:"\\regex_match:nnTF{a*?}{a}{A}{B}",expected:"expl3 regex pattern"},
 {name:"unicode",body:"\\regex_match:nnTF{a}{é}{A}{B}",expected:"expl3 regex subject"},
 {name:"assignment-scope",body:"\\globaldefs=-1 \\regex_match:nnTF{a}{a}{A}{B}",expected:"expl3 regex assignment scope"},
 {name:"redefined-builder",body:"\\def\\__regex_build:n#1{#1}\\regex_match:nnTF{a}{a}{A}{B}",expected:"expl3 regex builder"}];
[for(c in unsupported) (let r=profile.expand_with_kernel("\\ExplSyntaxOn" ++ c.body,kernel,options,100000,preload)^,
 {name:c.name,issue:r.issue,unsupported:r.unsupported,matches:r.issue=="tex-unsupported-primitive" and r.unsupported==c.expected})]
let document_options={*:options,constructor_argument_mode:true};
let document_preload=profile.preload_expl3_kernel(kernel,document_options,100000)^;
let document=profile.expand_with_kernel("\\ExplSyntaxOn\\regex_match:nnTF{a}{a}{A}{B}",kernel,document_options,100000,document_preload)^;
let tree=digest.digest(document)^;
{issue:tree.issue,unsupported:tree.unsupported}

let whitespace_pattern=[{kind:"control",value:"A"},{kind:"control",value:"s"},{kind:"control",value:"Z"}];
[for(code in [9,10,11,12,13,32]) (let r=regex.evaluate(whitespace_pattern,[{kind:"char",value:chr(code),cat:12}],100)^,
 {code:code,issue:r.issue,matches:r.matched==(code!=11)})]

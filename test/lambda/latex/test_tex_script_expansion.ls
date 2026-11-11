import reader: lambda.doc.latex.tex_engine.reader
import stream: lambda.doc.latex.tex_engine.stream
import expansion: lambda.doc.latex.tex_engine.expansion

fn test(source, budget = 100) any^ {
    let r = {*:reader.reader(source), endline: -1, catcodes: {'{': 1, '}': 2, '#': 6}};
    let expanded = expansion.expand(stream.from_reader(r), budget)^;
    {text: join([for (t in expanded.output) t.value], ""), issue: expanded.issue}
}
fn test_tokens(source) any^ {
    let r = {*:reader.reader(source), endline: -1, catcodes: {'{': 1, '}': 2, '#': 6}};
    let expanded = expansion.expand(stream.from_reader(r), 100)^;
    [for (t in expanded.output) [t.kind, t.value]]
}
fn test_categories(source) any^ {
    let r = {*:reader.reader(source), endline: -1, catcodes: {'{': 1, '}': 2, '#': 6}};
    let expanded = expansion.expand(stream.from_reader(r), 100)^;
    {tokens: [for (t in expanded.output) [t.kind, t.value, t.cat]], issue: expanded.issue}
}
fn test_messages(source) any^ {
    let r = {*:reader.reader(source), endline: -1, catcodes: {'{': 1, '}': 2, '#': 6}};
    let expanded = expansion.expand(stream.from_reader(r), 100)^;
    {messages: expanded.messages, issue: expanded.issue}
}
fn test_unsupported(source) any^ {
    let r = {*:reader.reader(source), endline: -1, catcodes: {'{': 1, '}': 2, '#': 6}};
    let expanded = expansion.expand(stream.from_reader(r), 100)^;
    [expanded.issue, expanded.unsupported]
}
"parameterless:"; test("\\def\\a{AB}\\a\\a")
"undelimited arguments:"; test("\\def\\f#1#2{#2#1#2}\\f{ab}c")
"nested argument groups:"; test("\\def\\id#1{#1}\\id{a{b}c}")
"nested definitions:"; test("\\def\\outer#1{\\def\\inner##1{#1##1}}\\outer{A}\\inner{B}")
"local definition:"; test("\\def\\x{A}{\\def\\x{B}\\x}\\x")
"recursive budget:"; test("\\def\\a{\\a}\\a", 3)
"delimited arguments:"; test("\\def\\f#1.#2.{(#1)(#2)}\\f x.y.")
"delimiter inside group:"; test("\\def\\f#1,{[#1]}\\f{a,b},")
"control delimiter:"; test("\\def\\f#1\\stop{[#1]}\\f ab\\stop")
"leading delimiter:"; test("\\def\\f ABC#1;{[#1]}\\f ABCx;")
"mismatched delimiter:"; test("\\def\\f ABC{Y}\\f ABX")
"brace delimiter:"; test("\\def\\f#1#{(#1)}\\f X{Y}")
"brace delimiter after text:"; test("\\def\\f#1.#{(#1)}\\f X.{Y}")
"expandafter:"; test("\\def\\id#1{[#1]}\\def\\x{XY}\\expandafter\\id\\x")
"noexpand:"; test_tokens("\\def\\x{XY}\\noexpand\\x\\x")
"noexpand assignment:"; test("\\noexpand\\def\\x{Y}\\x")
"global definition:"; test("\\def\\x{A}{\\def\\x{L}{\\gdef\\x{B}}}\\x")
"global prefix:"; test("\\def\\x{A}{\\global\\def\\x{B}}\\x")
"long argument:"; test_tokens("\\long\\def\\f#1{[#1]}\\f{a\\par b}")
"short argument:"; test("\\def\\f#1{[#1]}\\f{a\\par b}")
"changed catcode in a control word:"; test("\\catcode`\\!=11 \\def\\a!b{Y}\\a!b")
"local catcode:"; test_categories("{\\catcode`\\!=11 !}!")
"global catcode:"; test_categories("{\\global\\catcode`\\!=11 }!")
"expanded catcode number:"; test("\\def\\val{11}\\catcode`\\!=\\val \\def\\a!b{Y}\\a!b")
"budget while scanning a number:"; test("\\def\\loop{\\loop}\\catcode`\\!=\\loop", 3)
"constructed control sequence:"; test("\\expandafter\\def\\csname foobar\\endcsname{FB}\\csname foobar\\endcsname")
"expanded control sequence name:"; test("\\def\\part{foo}\\expandafter\\def\\csname foobar\\endcsname{FB}\\csname\\part bar\\endcsname")
"constructed undefined name:"; test("L\\csname unknown\\endcsname R")
"constructed primitive:"; test("\\csname def\\endcsname\\x{Y}\\x")
"expanded definition:"; test("\\def\\a{A}\\def\\b{B}\\edef\\x{\\a\\noexpand\\b}\\def\\a{Z}\\def\\b{Y}\\x")
"global expanded definition:"; test("\\def\\a{A}{\\xdef\\x{\\a}\\def\\a{B}}\\x")
"missing argument in expanded definition:"; test("\\def\\f#1{#1}\\edef\\x{\\f}")
"conditionals:"; test("\\iftrue A\\else B\\fi\\iffalse C\\else D\\fi")
"nested skipped conditional:"; test("\\iffalse\\iftrue X\\else Y\\fi Z\\else A\\fi")
"numeric conditionals:"; test("\\ifnum 3<4 T\\else F\\fi\\ifodd -3 O\\else E\\fi")
"defined conditionals:"; test("\\def\\x{A}\\ifdefined\\x Y\\else N\\fi\\ifdefined\\missing N\\else Y\\fi")
"incomplete conditional:"; test("\\iftrue A")
"unsupported conditional:"; test("\\ifdim 1pt=1pt T\\else F\\fi")
"let retains meaning:"; test("\\def\\a{A}\\let\\b=\\a\\def\\a{B}\\b\\a")
"let character:"; test("\\let\\a= Z\\a")
"let primitive:"; test("\\let\\condition=\\iftrue\\condition Y\\else N\\fi")
"let undefined:"; test("\\let\\a=\\missing\\ifdefined\\a N\\else Y\\fi")
"global let:"; test("\\def\\a{A}\\def\\b{B}{\\global\\let\\a=\\b}\\a")
"ifx macros:"; test("\\def\\a#1{[#1]}\\def\\b#1{[#1]}\\ifx\\a\\b Y\\else N\\fi")
"ifx undefined:"; test("\\ifx\\missing\\absent Y\\else N\\fi")
"ifx different macros:"; test("\\def\\a#1{[#1]}\\def\\b#1{(#1)}\\ifx\\a\\b N\\else Y\\fi")
"constructed name is relax:"; test("\\csname missing\\endcsname\\expandafter\\ifx\\csname missing\\endcsname\\relax Y\\else N\\fi")
"if and ifcat:"; test("\\if aa T\\else F\\fi\\ifcat a1 F\\else T\\fi")
"ifcsname does not define:"; test("\\ifcsname missing\\endcsname N\\else Y\\fi\\ifdefined\\missing N\\else Y\\fi")
"ifcsname finds macro:"; test("\\def\\yes{A}\\ifcsname yes\\endcsname Y\\else N\\fi")
"string control sequence:"; test("\\def\\x{XY}\\string\\x")
"integer conversion:"; test("\\number -+-5,\\number\"FF,\\number'777")
"number expands its argument:"; test("\\def\\n{12}\\number\\n")
"terminal write:"; test_messages("\\def\\T#1{\\immediate\\write16{LT:#1}}\\def\\a#1{[#1]}\\T{\\a x\\a{yz}}\\end ignored")
"meaning of macro:"; test("\\def\\x{X}\\edef\\z{\\noexpand\\x Y}\\meaning\\z")
"meaning of character:"; test("\\meaning a")
"roman numerals:"; test("\\romannumeral 1984 \\romannumeral 0 \\romannumeral 4999")
"unsupported primitive name:"; test_unsupported("\\dimen0=1pt")
"count register:"; test("\\count255=17 \\the\\count255")
"local count:"; test("\\count0=3 {\\count0=4 \\the\\count0}\\the\\count0")
"global count:"; test("\\count0=3 {\\global\\count0=4 }\\the\\count0")
"countdef:"; test("\\countdef\\n=12 \\n=9 \\number\\n")
"countdef equality:"; test("\\countdef\\a=1 \\countdef\\b=1 \\ifx\\a\\b Y\\else N\\fi")
"token register expands at use:"; test("\\def\\x{A}\\toks0={\\x}\\def\\x{B}\\the\\toks0")
"token register in edef:"; test("\\def\\x{A}\\toks0={\\x}\\edef\\z{\\the\\toks0}\\def\\x{B}\\z")
"token register copy:"; test("\\toks0={AB}\\toks1=\\toks0 \\the\\toks1")
"token register local:"; test("\\toks0={A}{\\toks0={B}\\the\\toks0}\\the\\toks0")
"token register global:"; test("\\toks0={A}{\\global\\toks0={B}}\\the\\toks0")
"token register alias:"; test("\\toksdef\\t=4 \\t={AB}\\the\\t")
"character definition:"; test("\\chardef\\c=65 \\c,\\number\\c,\\meaning\\c")
"math character definition:"; test("\\mathchardef\\m=\"7161 \\meaning\\m")
"character aliases:"; test("\\chardef\\c=65 \\let\\d=\\c\\ifx\\c\\d Y\\else N\\fi")
"case shift before expansion:"; test("\\def\\x{X}\\def\\id#1{#1}\\uppercase{\\id{abc\\x}}")
"custom lowercase code:"; test("\\lccode`Z=`q \\lowercase{XYZ}")
"local lowercase code:"; test("{\\lccode`Z=`q \\lowercase{Z}}\\lowercase{Z}")
"global lowercase code:"; test("{\\global\\lccode`Z=`q }\\lowercase{Z}")
"futurelet:"; test("\\def\\check{\\ifx\\next a T\\else F\\fi}\\futurelet\\next\\check a")
"afterassignment:"; test("\\def\\after{X}\\afterassignment\\after\\count0=1 Y")
"catcode quantity:"; test("\\catcode`\\!=11 \\the\\catcode`\\!")
"escapechar:"; test("\\escapechar=-1 \\string\\x,\\escapechar=`/ \\string\\x")
"local escapechar:"; test("{\\escapechar=-1 \\string\\x}\\string\\x")

"protected definition:"; test("\\protected\\def\\p{P}\\edef\\x{\\p}\\meaning\\x")
"protected expansion:"; test("\\protected\\def\\p{P}\\edef\\x{\\p}\\x")
"unexpanded definition:"; test("\\def\\p{A}\\edef\\x{\\unexpanded{\\p}}\\def\\p{B}\\x")
"detokenize:"; test("\\detokenize{\\x a#b}")
"numexpr precedence:"; test("\\number\\numexpr 2+3*4\\relax,\\number\\numexpr(2+3)*4\\relax")
"numexpr rounding:"; test("\\number\\numexpr -3/2\\relax,\\the\\numexpr 3*5/2\\relax")
"numexpr scaling:"; test("\\number\\numexpr 2147483647*2/2\\relax")
"scantokens:"; test("\\def\\x{X}\\edef\\z{\\scantokens{\\x}}\\z")
"scantokens shared catcodes:"; test("\\scantokens{\\catcode`\\!=11 }\\def\\a!b{Y}\\a!b")
"scantokens eof hook:"; test("\\everyeof{\\noexpand}\\def\\x{X}\\edef\\z{\\scantokens{\\x}}\\def\\x{Y}\\z")
"scantokens eof insertion:"; test("\\everyeof{Z}\\scantokens{A}")
"scantokens local eof hook:"; test("{\\everyeof{Z}\\scantokens{A}}\\scantokens{B}")
"count arithmetic:"; test("\\count1=10 \\advance\\count1 by 5 \\multiply\\count1 3 \\divide\\count1 by 4 \\the\\count1")
"count division:"; test("\\count1=-7 \\divide\\count1 2 \\the\\count1")
"count alias arithmetic:"; test("\\countdef\\x=20 \\x=41 \\advance\\x 1 \\the\\count20")
"count arithmetic scopes:"; test("\\count0=3 {\\advance\\count0 2 \\the\\count0}{\\global\\multiply\\count0 by 4 }\\the\\count0")
"count arithmetic afterassignment:"; test("\\count0=3 \\afterassignment X\\advance\\count0 by 2 \\the\\count0")
"count arithmetic zero divisor:"; test("\\count0=3 \\divide\\count0 by 0 \\the\\count0")
"count arithmetic overflow:"; test("\\count0=2147483647 \\advance\\count0 by 1 \\the\\count0")
"aftergroup order:"; test("{\\aftergroup A\\aftergroup B}C")
"nested aftergroup:"; test("{\\aftergroup A{\\aftergroup B}C}D")
"explicit group:"; test("\\count0=3 \\begingroup \\count0=7 \\aftergroup X\\endgroup \\the\\count0")
"global in explicit group:"; test("\\begingroup \\global\\count0=7 \\endgroup \\the\\count0")
"mismatched group end:"; test("{\\endgroup}")
"case conditional branches:"; test("\\ifcase 0 A\\or B\\else C\\fi,\\ifcase 1 A\\or B\\else C\\fi,\\ifcase 2 A\\or B\\else C\\fi")
"case conditional negative:"; test("\\ifcase -1 A\\or B\\else C\\fi")
"nested case skipping:"; test("\\ifcase 1 \\ifnum 1=1 A\\or B\\fi \\or C\\or D\\fi")
"case without matching branch:"; test("\\ifcase 3 A\\or B\\fi C")
"unless conditionals:"; test("\\unless\\ifnum 1=2 T\\else F\\fi,\\unless\\iftrue F\\else T\\fi")
"unless nested scan:"; test("\\unless\\ifnum\\iftrue 1\\else 2\\fi=1 F\\else T\\fi")
"unless defined:"; test("\\unless\\ifdefined\\x T\\fi")
"invalid unless:"; test("\\unless\\ifcase 0 A\\fi")

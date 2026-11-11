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
fn test_captured(source, captured) any^ {
    let r = {*:reader.reader(source), endline: -1, catcodes: {'{': 1, '}': 2, '#': 6}};
    let expanded = expansion.expand(stream.from_reader(r), 100, {resources: {local: captured}})^;
    {text: join([for (t in expanded.output) t.value], ""), issue: expanded.issue,
        resource_count: expanded.resource_count, resource_bytes: expanded.resource_bytes}
}
fn test_resource_budget() any^ {
    let entries = map([for (i in 0 to 128, part in [string(i), {data: "x"}]) part]);
    let source = join([for (i in 0 to 128) "\\pdffilesize{" ++ string(i) ++ "}"], "");
    let r = {*:reader.reader(source), endline: -1, catcodes: {'{': 1, '}': 2, '#': 6}};
    let expanded = expansion.expand(stream.from_reader(r), 100000, {resources: {local: entries}})^;
    {issue: expanded.issue, resource_count: expanded.resource_count, resource_bytes: expanded.resource_bytes}
}
fn test_fonts(source) any^ {
    let r = {*:reader.reader(source), endline: -1, catcodes: {'{': 1, '}': 2, '#': 6}};
    let expanded = expansion.expand(stream.from_reader(r), 1000)^;
    {text: join([for (t in expanded.output) t.value], ""), issue: expanded.issue,
        diagnostics: expanded.diagnostics, font_memory: expanded.font_memory}
}
fn test_input_sources(source, captured, origin = "local", budget = 1000) any^ {
    let r = {*:reader.reader(source), origin: origin, endline: -1, catcodes: {'{': 1, '}': 2, '#': 6}};
    let expanded = expansion.expand(stream.from_reader(r), budget, {resources: captured, source_name: "main.tex"})^;
    {text: join([for (t in expanded.output) t.value], ""), issue: expanded.issue,
        diagnostics: expanded.diagnostics, resource_count: expanded.resource_count,
        sources: [for (item in expanded.sources) [item.id, item.name, item.origin]],
        positions: [for (t in expanded.output) [t.value, t.file, t.start, t.end]]}
}
fn test_latex(source) any^ {
    let r = {*:reader.reader(source), endline: -1,
        catcodes: {'{': 1, '}': 2, '$': 3, '&': 4, '#': 6, '^': 7, '_': 8, '~': 13}};
    let expanded = expansion.expand(stream.from_reader(r), 1000, {latex: true})^;
    {text: join([for (t in expanded.output) t.value], ""), issue: expanded.issue,
        unsupported: expanded.unsupported}
}
fn test_latex_details(source) any^ {
    let r = {*:reader.reader(source), endline: -1,
        catcodes: {'{': 1, '}': 2, '$': 3, '&': 4, '#': 6, '^': 7, '_': 8, '~': 13}};
    let expanded = expansion.expand(stream.from_reader(r), 1000, {latex: true})^;
    {text: join([for (t in expanded.output) t.value], ""), issue: expanded.issue, diagnostics: expanded.diagnostics}
}
fn test_latex_document(source) any^ {
    let r = {*:reader.reader(source), endline: -1,
        catcodes: {'{': 1, '}': 2, '$': 3, '&': 4, '#': 6, '^': 7, '_': 8, '~': 13}};
    let expanded = expansion.expand(stream.from_reader(r), 1000, {latex: true})^;
    {text: join([for (t in expanded.output) t.value], ""), issue: expanded.issue,
        diagnostics: expanded.diagnostics, trailing: expanded.trailing_source.text}
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
"dimension conditional:"; test("\\ifdim 1pt=1pt T\\else F\\fi")
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
"unsupported primitive name:"; test_unsupported("\\setbox0=\\hbox{}")
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
"dimension units:"; test("\\dimen0=1in \\dimen1=1cm \\dimen2=1mm \\the\\dimen0,\\the\\dimen1,\\the\\dimen2")
"dimension rounding:"; test("\\dimen0=0.333333pt \\dimen1=16383.99999pt \\the\\dimen0,\\the\\dimen1")
"dimension arithmetic:"; test("\\dimen0=1.5pt \\advance\\dimen0 by 2.25pt \\multiply\\dimen0 2 \\divide\\dimen0 by 4 \\the\\dimen0")
"dimension scaled value:"; test("\\dimen0=100sp \\number\\dimen0,\\the\\dimen0")
"dimension register factors:"; test("\\dimen0=3.75pt \\dimen1=-.5\\dimen0 \\the\\dimen1")
"dimension local and global:"; test("\\dimen0=1pt {\\dimen0=2pt \\the\\dimen0}{\\global\\dimen0=3pt }\\the\\dimen0")
"dimension alias:"; test("\\dimendef\\d=20 \\d=2pt \\global\\multiply\\d 3 \\the\\d,\\the\\dimen20")
"dimension sign comparison:"; test("\\ifdim -1pt<-.5pt T\\else F\\fi\\unless\\ifdim 1in=1cm T\\fi")
"dimension overflow:"; test("\\dimen0=16384pt \\the\\dimen0")
"dimension missing unit:"; test("\\dimen0=1 .5pt")
"dimension rounding reference:"; test("\\dimen0=.00000762939453125pt \\dimen1=1.123456789012345678pt \\the\\dimen0,\\the\\dimen1")
"dimension fractional units:"; test("\\dimen0=1.5cm \\dimen1=.5sp \\the\\dimen0,\\the\\dimen1")
"negative dimension factor:"; test("\\dimen0=-3.75pt \\dimen1=.5\\dimen0 \\the\\dimen1")
"empty box tests:"; test("\\ifvoid0 V\\fi\\ifhbox0 H\\else h\\fi\\ifvbox0 V\\else v\\fi")
"negated empty box:"; test("\\unless\\ifvoid0 N\\else Y\\fi")
"box register range:"; test("\\ifvoid32768 N\\fi")
"glue orders:"; test("\\skip0=1pt plus 2fil minus 3fill \\skip1=\\skip0 \\advance\\skip1 1pt plus 1fill \\relax\\the\\skip1")
"glue scaling:"; test("\\skip0=2pt plus 3fil minus 4pt \\multiply\\skip0 by -2 \\divide\\skip0 by 4 \\the\\skip0")
"glue copied negative:"; test("\\skip0=1pt plus 2fil minus 3pt \\skip1=-\\skip0 \\the\\skip1")
"glue scoped:"; test("\\skip0=1pt {\\skip0=2pt \\relax\\the\\skip0}{\\global\\skip0=3pt }\\the\\skip0")
"glue alias:"; test("\\skipdef\\g=3 \\g=1pt plus 1fil \\advance\\g 2pt \\relax\\the\\skip3")
"math glue:"; test("\\muskip0=3mu plus 2fill \\muskipdef\\m=3 \\m=\\muskip0 \\the\\m")
"glue division by zero:"; test("\\skip0=1pt \\divide\\skip0 0")
"glue overflow:"; test("\\skip0=16383pt \\multiply\\skip0 2")
"default character codes:"; test("\\the\\sfcode`A,\\the\\sfcode`a,\\the\\mathcode`a,\\the\\mathcode`1,\\the\\delcode`.,\\the\\delcode`a")
"character codes scoped:"; test("\\sfcode`A=222 {\\sfcode`A=333 \\the\\sfcode`A}{\\global\\sfcode`A=444 }\\the\\sfcode`A")
"math and delimiter assignment:"; test("\\mathcode`a=1234 \\delcode`a=-1 \\the\\mathcode`a,\\the\\delcode`a")
"character code range:"; test("\\sfcode`a=32768")
"integer parameter scope:"; test("\\tolerance=20 {\\tolerance=30 \\the\\tolerance}{\\global\\tolerance=40 }\\the\\tolerance")
"true dimension magnification:"; test("\\mag=2000 \\dimen0=1.5truept \\dimen1=1.5pt \\the\\dimen0,\\the\\dimen1")
"dimension expression precedence:"; test("\\the\\dimexpr 1pt+2pt*3\\relax,\\the\\dimexpr(1pt+2pt)*3\\relax")
"dimension expression rounding:"; test("\\the\\dimexpr 1sp*3/2\\relax,\\the\\dimexpr -1sp*3/2\\relax")
"dimension expression as integer:"; test("\\number\\dimexpr 1pt+1sp\\relax")
"glue expression orders:"; test("\\the\\glueexpr 1pt plus 2fil+2pt plus 3fill\\relax")
"math glue expression:"; test("\\the\\muexpr(3mu plus 2fil)*2\\relax")
"glue expression assignment:"; test("\\skip0=\\glueexpr 1pt plus 1fil*2\\relax \\relax\\the\\skip0")
"glue expression rounding:"; test("\\the\\glueexpr 1sp plus 1sp*3/2\\relax")
"expression divide by zero:"; test("\\the\\dimexpr 1pt/0\\relax")
"dimension expression overflow:"; test("\\the\\dimexpr 16383pt*2\\relax")
"pdf checksum:"; test("\\pdfmdfivesum{abc}")
"pdf checksum file unsupported:"; test_unsupported("\\pdfmdfivesum file{missing.tex}")
"captured file queries:"; test_captured("\\pdffilesize{a.tex},\\pdfmdfivesum file{a.tex},\\pdffilemoddate{a.tex}", {'a.tex': {data: "abc"}})
"captured empty and unicode files:"; test_captured("\\pdffilesize{empty},\\pdffilesize{utf8}", {empty: {data: ""}, utf8: {data: "é"}})
"missing captured file:"; test_captured("[\\pdffilesize{missing}][\\pdfmdfivesum file{missing}][\\pdffilemoddate{missing}]", {})
"captured path restrictions:"; test_captured("[\\pdffilesize{../secret}][\\pdffilesize{/secret}]", {'../secret': {data: "abc"}, '/secret': {data: "abc"}})
"unavailable file source:"; test_unsupported("\\pdffilesize{missing}")
"fixed creation date:"; test("\\pdfcreationdate")
"captured binary checksum:"; test_captured("\\pdffilesize{bin},\\pdfmdfivesum file{bin}", {bin: {data: binary([0, 255])^}})
"invalid captured resource:"; test_captured("\\pdffilesize{bad}", {bad: {data: 42}})
"captured file budget:"; test_resource_budget()
"font parameter growth:"; test_fonts("\\font\\a=cmr10 at 1sp \\font\\b=cmr10 at 2sp \\fontdimen100\\b=5sp \\the\\fontdimen100\\b,\\the\\fontdimen8\\a")
"font identity and reuse:"; test_fonts("\\font\\a=cmr10 at 10pt \\font\\b=cmr10 at 10pt \\ifx\\a\\b Y\\else N\\fi,\\meaning\\a")
"font scaled metrics:"; test_fonts("\\font\\a=cmr10 scaled 2000 \\the\\fontdimen6\\a")
"font selection scope:"; test_fonts("\\font\\a=cmr10 at 10pt \\a {\\nullfont \\the\\fontdimen6\\font}\\the\\fontdimen6\\font")
"font parameter global storage:"; test_fonts("\\font\\a=cmr10 {\\fontdimen6\\a=2pt }\\the\\fontdimen6\\a")
"font definition scoped:"; test_fonts("{\\font\\a=cmr10 }\\ifdefined\\a N\\else Y\\fi")
"font capacity:"; test_fonts("\\font\\a=cmr10 \\fontdimen8000000\\a=1pt")
"font standard metrics:"; test_fonts("\\font\\a=cmr10 \\relax\\the\\fontdimen2\\a,\\the\\fontdimen6\\a")
"font selected afterassignment:"; test_fonts("\\font\\a=cmr10 \\afterassignment X\\a Y")
"font names:"; test_fonts("\\font\\a={cmr10} at 10pt \\font\\b=\"cmr10\" at 10pt \\ifx\\a\\b Y\\else N\\fi")
"input extension and positions:"; test_input_sources("A\\input{chapter}B", {local: {'chapter.tex': {data: "éX"}}})
"input exact before extension:"; test_input_sources("\\input{chapter}", {local: {chapter: {data: "X"}, 'chapter.tex': {data: "Y"}}})
"nested inputs and definitions:"; test_input_sources("\\input{a}\\x", {local: {a: {data: "\\def\\x{A}\\input{b}\\x"}, b: {data: "\\gdef\\x{B}"}}})
"reopened input identity:"; test_input_sources("\\input{a}\\input{a}", {local: {a: {data: "X"}}})
"bundled input scope:"; test_input_sources("\\input{same}", {local: {same: {data: "L"}}, bundled: {same: {data: "B"}}}, "bundled")
"local input scope:"; test_input_sources("\\input{same}", {local: {same: {data: "L"}}, bundled: {same: {data: "B"}}})
"endinput nested:"; test_input_sources("R\\input{early}Z", {local: {early: {data: "A\\endinput B\nC"}}})
"endinput root:"; test_input_sources("A\\endinput B\nC", {local: {}})
"input eof hook:"; test_input_sources("\\everyeof{Z}\\input{a}", {local: {a: {data: "X"}}})
"missing input:"; test_input_sources("A\\input{missing}B", {local: {}})
"outside input:"; test_input_sources("A\\input{../bad}B", {local: {'../bad': {data: "X"}}})
"recursive input budget:"; test_input_sources("\\input{a}", {local: {a: {data: "\\input{a}"}}}, "local", 4)
"input shares category codes:"; test_input_sources("\\input{cats}\\def\\a!b{Y}\\a!b", {local: {cats: {data: "\\catcode`\\!=11 "}}})
"nested bundled origin:"; test_input_sources("\\input{outer}", {local: {inner: {data: "L"}}, bundled: {outer: {data: "\\input{inner}"}, inner: {data: "B"}}}, "bundled")
"local cannot read bundled:"; test_input_sources("\\input{secret}", {local: {}, bundled: {secret: {data: "B"}}})
"quoted input name:"; test_input_sources("\\input{\"with space\"}", {local: {'with space': {data: "Y"}}})
"input provider unavailable:"; test_unsupported("\\input{missing}")
"integer parameter defaults:"; test("\\the\\globaldefs,\\the\\hangafter,\\the\\year")
"dimension parameter arithmetic:"; test("\\parindent=3pt \\advance\\parindent by 1pt \\relax\\the\\parindent")
"glue parameter arithmetic:"; test("\\baselineskip=1pt plus 1fil \\advance\\baselineskip 2pt plus 1fill \\relax\\the\\baselineskip")
"token parameters:"; test("\\everyjob={AB}\\toks0=\\everyjob \\the\\toks0")
"positive globaldefs:"; test("{\\globaldefs=1 \\def\\x{A}\\parindent=2pt }\\x,\\the\\parindent")
"negative globaldefs:"; test("{\\globaldefs=-1 \\global\\def\\x{A}}\\ifdefined\\x N\\else Y\\fi")
"latex newif:"; test_latex("\\newif\\iffoo \\iffoo F\\else A\\fi\\footrue\\iffoo B\\else F\\fi\\foofalse\\iffoo F\\else C\\fi")
"latex newif scope:"; test_latex("\\newif\\iffoo {\\footrue\\iffoo A\\fi}\\iffoo F\\else B\\fi")
"latex constructor:"; test_latex("\\lambdaconstructor\\section \\ifdefined\\section Y\\fi\\section{Title}")
"latex primitive explicitly unsupported:"; test_latex("\\documentclass{article}")
"latex newcommand arguments:"; test_latex_details("\\newcommand{\\pair}[2]{#1/#2}\\pair{left}{right}")
"latex optional argument:"; test_latex_details("\\newcommand\\greet[2][World]{Hi #1/#2}\\greet{A},\\greet[Sam]{B}")
"latex nested optional group:"; test_latex_details("\\newcommand\\x[1][D]{(#1)}\\x[{a]b}]")
"latex renew and provide:"; test_latex_details("\\newcommand\\x{A}\\providecommand\\x{B}\\renewcommand\\x{C}\\x")
"latex duplicate definition:"; test_latex_details("\\newcommand\\x{A}\\newcommand\\x{B}\\x")
"latex missing renewal:"; test_latex_details("\\renewcommand\\x{A}\\x")
"latex starred short argument:"; test_latex_details("\\newcommand*\\x[1]{#1}\\x{a\\par b}")
"latex long argument:"; test_latex_details("\\newcommand\\x[1]{#1}\\x{a\\par b}")
"latex robust definition:"; test_latex_details("\\DeclareRobustCommand\\x{A}\\edef\\y{\\x}\\meaning\\y")
"latex optional macro identity:"; test_latex_details("\\newcommand\\x[1][A]{#1}\\newcommand\\y[1][B]{#1}\\ifx\\x\\y N\\else Y\\fi")
"latex macro identity with tex def:"; test_latex_details("\\def\\y#1{#1}\\newcommand*\\x[1]{#1}\\ifx\\x\\y Y\\else N\\fi")
"latex custom environment:"; test_latex_document("\\newenvironment{wrap}[1]{<#1>}{</>}\\begin{wrap}{A}X\\end{wrap}")
"latex environment optional:"; test_latex_document("\\newenvironment{wrap}[1][D]{(#1)}{!}\\begin{wrap}X\\end{wrap},\\begin{wrap}[Z]Y\\end{wrap}")
"latex environment scope:"; test_latex_document("\\newenvironment{e}{\\def\\inside{Y}}{\\inside}\\begin{e}X\\end{e}\\ifdefined\\inside N\\else Z\\fi")
"latex passthrough environment:"; test_latex_document("\\begin{quote}Hi\\end{quote}")
"latex environment mismatch:"; test_latex_document("\\begin{a}X\\end{b}Y\\end{a}")
"latex environment expanded name:"; test_latex_document("\\def\\n{e}\\newenvironment{e}{A}{B}\\begin{\\n}X\\end{\\n}")
"latex document tail:"; test_latex_document("\\begin{document}X\\end{document}%keep\n\\unexpanded{tail}")
"latex environment aftergroup:"; test_latex_document("\\newenvironment{e}{\\aftergroup Z A}{B}\\begin{e}X\\end{e}")
"latex nested environments:"; test_latex_document("\\newenvironment{a}{A}{a}\\newenvironment{b}{B}{b}\\begin{a}X\\begin{b}Y\\end{b}Z\\end{a}")
"latex renew environment:"; test_latex_document("\\newenvironment{e}{A}{B}\\renewenvironment{e}{C}{D}\\begin{e}X\\end{e}")
"latex duplicate environment:"; test_latex_document("\\newenvironment{e}{A}{B}\\newenvironment{e}{C}{D}\\begin{e}X\\end{e}")
"latex undefined renewal:"; test_latex_document("\\renewenvironment{e}{A}{B}\\begin{e}X\\end{e}")
"latex unterminated environment argument:"; test_latex_document("\\begin{broken")
"latex document hooks:"; test_latex_document("\\AtBeginDocument{A}\\AtBeginDocument{B}\\AtEndDocument{C}\\AtEndDocument{D}\\begin{document}X\\end{document}TAIL")
"latex late begin hook:"; test_latex_document("\\begin{document}A\\AtBeginDocument{B}C\\end{document}")
"latex hook definition:"; test_latex_document("\\AtBeginDocument{\\def\\x{Y}}\\begin{document}\\x\\end{document}")
"math definition scope:"; test_latex("\\def\\x{A}$\\def\\x{B}\\x$\\x")
"math mode conditionals:"; test_latex("\\ifhmode H\\fi$\\ifmmode M\\fi\\ifinner I\\fi$\\ifhmode H\\fi$$\\ifmmode M\\fi\\ifinner X\\else D\\fi$$")
"math hooks:"; test_latex("\\everymath={A}\\everydisplay={B}$x$$$y$$")
"math global assignment:"; test_latex("\\def\\x{A}$\\global\\def\\x{B}$\\x")
"latex math delimiter scope:"; test_latex("\\def\\x{A}\\(\\def\\x{B}\\x\\)\\x")
"display missing second shift:"; test_latex_details("$$a$X")
"latex display delimiters:"; test_latex("\\[\\ifmmode M\\fi\\ifinner I\\fi\\]\\ifmmode X\\else N\\fi")
"nested math display mode:"; test_latex("$$\\ifinner X\\else A\\fi\\(\\ifinner X\\else B\\fi\\)\\ifinner X\\else C\\fi$$")
"control space token:"; test_tokens("A\\ B")
"control space alias:"; test_tokens("\\let\\sp=\\ A\\sp B")
"macro control space:"; test_tokens("\\def\\sp{\\ }A\\sp B")

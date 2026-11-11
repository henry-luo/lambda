import profile: lambda.doc.latex.tex_engine.profile
let kernel = input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
fn report(source) any^ {
    let result = profile.expand_with_kernel(source, kernel, {script_expl3: true, echo_definitions: true}, 100000)^;
    let text = join([for (token in result.output) token.value], "");
    {text: text, issue: result.issue, unsupported: result.unsupported,
        matches_native: if (result.issue != null) null else text == (parse(source, {type: "tex"})^.text)}
}
"sequence from comma list:"; report("\\ExplSyntaxOn\\seq_new:N\\l_test_seq\\seq_set_from_clist:Nn\\l_test_seq{a,b,c}\\seq_use:Nn\\l_test_seq{,~}\\ExplSyntaxOff")
"empty and grouped entries:"; report("\\ExplSyntaxOn\\seq_new:N\\l_test_seq\\seq_set_from_clist:Nn\\l_test_seq{a,,{b,c},~{~x~}~,{},d}\\seq_count:N\\l_test_seq/\\seq_use:Nn\\l_test_seq{|}\\ExplSyntaxOff")
"empty sequence:"; report("\\ExplSyntaxOn\\seq_new:N\\l_test_seq[\\seq_use:Nn\\l_test_seq{,}]/\\seq_count:N\\l_test_seq\\ExplSyntaxOff")
"local push:"; report("\\ExplSyntaxOn\\seq_new:N\\l_test_seq\\seq_put_left:Nn\\l_test_seq{a}{\\seq_put_right:Nn\\l_test_seq{b}\\seq_use:Nn\\l_test_seq{,}}/\\seq_use:Nn\\l_test_seq{,}\\ExplSyntaxOff")
"global push:"; report("\\ExplSyntaxOn\\seq_new:N\\l_test_seq\\seq_put_right:Nn\\l_test_seq{a}{\\seq_gput_left:Nn\\l_test_seq{b}}\\seq_use:Nn\\l_test_seq{,}\\ExplSyntaxOff")
"global list and clear:"; report("\\ExplSyntaxOn\\seq_new:N\\l_test_seq{\\seq_gset_from_clist:Nn\\l_test_seq{a,b}}\\seq_use:Nn\\l_test_seq{,}{\\seq_gclear:N\\l_test_seq}[\\seq_use:Nn\\l_test_seq{,}]\\ExplSyntaxOff")
"token preservation:"; report("\\ExplSyntaxOn\\cs_new:Npn\\word:{A}\\seq_new:N\\l_test_seq\\seq_put_right:Nn\\l_test_seq{\\word:}\\edef\\saved{\\seq_use:Nn\\l_test_seq{,}}\\cs_set:Npn\\word:{B}\\saved\\ExplSyntaxOff")
"ascending sort:"; report("\\ExplSyntaxOn\\seq_new:N\\l_test_seq\\seq_set_from_clist:Nn\\l_test_seq{c,a,b}\\seq_sort:Nn\\l_test_seq{\\str_compare:nNnTF{#1}>{#2}{\\sort_return_swapped:}{\\sort_return_same:}}\\seq_use:Nn\\l_test_seq{,}\\ExplSyntaxOff")
"descending sort:"; report("\\ExplSyntaxOn\\seq_new:N\\l_test_seq\\seq_set_from_clist:Nn\\l_test_seq{b,c,a}\\seq_sort:Nn\\l_test_seq{\\str_compare:nNnTF{#1}<{#2}{\\sort_return_swapped:}{\\sort_return_same:}}\\seq_use:Nn\\l_test_seq{|}\\ExplSyntaxOff")
"stable sort:"; report("\\ExplSyntaxOn\\seq_new:N\\l_test_seq\\seq_set_from_clist:Nn\\l_test_seq{c,a,b}\\seq_sort:Nn\\l_test_seq{\\sort_return_same:}\\seq_use:Nn\\l_test_seq{,}\\ExplSyntaxOff")
"sort group scope:"; report("\\ExplSyntaxOn\\cs_new:Npn\\value:{O}\\seq_new:N\\l_test_seq\\seq_set_from_clist:Nn\\l_test_seq{b,a}\\seq_sort:Nn\\l_test_seq{\\cs_set:Npn\\value:{X}\\sort_return_same:}\\value:\\ExplSyntaxOff")
"comparison order:"; report("\\ExplSyntaxOn\\cs_new:Npn\\trace:{}\\seq_new:N\\l_test_seq\\seq_set_from_clist:Nn\\l_test_seq{e,d,c,b,a}\\seq_sort:Nn\\l_test_seq{\\xdef\\trace:{\\trace:[#1/#2]}\\str_compare:nNnTF{#1}>{#2}{\\sort_return_swapped:}{\\sort_return_same:}}\\trace:\\seq_use:Nn\\l_test_seq{,}\\ExplSyntaxOff")
"unexpanded string comparison:"; report("\\ExplSyntaxOn\\str_compare:nNnTF{\\foo}<{\\zoo}{Y}{N}\\ExplSyntaxOff")
"string comparison variants:"; report("\\ExplSyntaxOn\\str_compare:nNnT{a}<{b}{T}\\str_compare:nNnF{a}>{b}{F}\\ExplSyntaxOff")
"invalid sequence:"; report("\\ExplSyntaxOn\\seq_use:Nn\\l_missing_seq{,}")
"duplicate sequence:"; report("\\ExplSyntaxOn\\seq_new:N\\l_test_seq\\seq_new:N\\l_test_seq")
"missing sort return:"; report("\\ExplSyntaxOn\\seq_new:N\\l_test_seq\\seq_set_from_clist:Nn\\l_test_seq{a,b}\\seq_sort:Nn\\l_test_seq{\\relax}")
"duplicate sort return:"; report("\\ExplSyntaxOn\\seq_new:N\\l_test_seq\\seq_set_from_clist:Nn\\l_test_seq{a,b}\\seq_sort:Nn\\l_test_seq{\\sort_return_same:\\sort_return_swapped:}")
let fixture_style = input("test/lambda/latex/fixtures/local_packages/lmdexpl.sty", "text")^;
let fixture_source = "\\usepackage{lmdexpl}\\greet/\\greet[Jane]/\\twice{A}/\\sumof{8}{5}/\\sorted{pear,apple,kiwi}/\\ready";
let fixture = profile.expand_with_kernel(fixture_source, kernel, {script_expl3: true,
    resources: {local: {'lmdexpl.sty': {data: fixture_style}}, bundled: {}}}, 100000)^;
"project expl3 fixture:"; {text: join([for (token in fixture.output) token.value], ""),
    issue: fixture.issue, unsupported: fixture.unsupported}
fn order_case(size) any^ {
    let values = slice(["i", "h", "g", "f", "e", "d", "c", "b", "a"], 0, size)^;
    let source = "\\ExplSyntaxOn\\cs_new:Npn\\trace:{}\\seq_new:N\\l_test_seq\\seq_set_from_clist:Nn\\l_test_seq{" ++
        join(values, ",") ++ "}\\seq_sort:Nn\\l_test_seq{\\xdef\\trace:{\\trace:[#1/#2]}" ++
        "\\str_compare:nNnTF{#1}>{#2}{\\sort_return_swapped:}{\\sort_return_same:}}\\trace:\\seq_use:Nn\\l_test_seq{,}\\ExplSyntaxOff";
    let r = report(source)^;
    [size, r.issue, r.matches_native]
}
"comparison order across lengths:"; [for (n in 2 to 9) order_case(n)^]
fn fixture_call(name) any^ {
    let r = profile.expand_with_kernel("\\usepackage{lmdexpl}\\" ++ name, kernel, {script_expl3: true,
        resources: {local: {'lmdexpl.sty': {data: fixture_style}}, bundled: {}}}, 100000)^;
    {issue: r.issue, unsupported: r.unsupported}
}
"fixture floating point:"; fixture_call("roottwo")
"fixture case conversion:"; fixture_call("shout{Hello}")
"separator token preservation:"; report("\\ExplSyntaxOn\\cs_new:Npn\\sep:{A}\\seq_new:N\\l_test_seq\\seq_set_from_clist:Nn\\l_test_seq{x,y}\\edef\\saved{\\seq_use:Nn\\l_test_seq{\\sep:}}\\cs_set:Npn\\sep:{B}\\saved\\ExplSyntaxOff")
"global sort:"; report("\\ExplSyntaxOn\\seq_new:N\\l_test_seq\\seq_set_from_clist:Nn\\l_test_seq{b,a}{\\seq_gsort:Nn\\l_test_seq{\\str_compare:nNnTF{#1}>{#2}{\\sort_return_swapped:}{\\sort_return_same:}}}\\seq_use:Nn\\l_test_seq{,}\\ExplSyntaxOff")
"sequence aliases copy values:"; report("\\ExplSyntaxOn\\seq_new:N\\l_first_seq\\seq_new:N\\l_second_seq\\seq_put_right:Nn\\l_first_seq{A}\\let\\l_second_seq=\\l_first_seq\\seq_put_right:Nn\\l_first_seq{B}\\seq_use:Nn\\l_second_seq{,}/\\seq_use:Nn\\l_first_seq{,}\\ExplSyntaxOff")
"nested sorting:"; report("\\ExplSyntaxOn\\seq_new:N\\l_outer_seq\\seq_new:N\\l_inner_seq\\seq_set_from_clist:Nn\\l_outer_seq{b,a}\\seq_set_from_clist:Nn\\l_inner_seq{y,x}\\seq_sort:Nn\\l_outer_seq{\\seq_sort:Nn\\l_inner_seq{\\str_compare:nNnTF{##1}>{##2}{\\sort_return_swapped:}{\\sort_return_same:}}\\str_compare:nNnTF{#1}>{#2}{\\sort_return_swapped:}{\\sort_return_same:}}\\seq_use:Nn\\l_outer_seq{,}/\\seq_use:Nn\\l_inner_seq{,}\\ExplSyntaxOff")

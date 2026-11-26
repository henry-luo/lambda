import compare: ._tex_script_recovery
let native=input("test/lambda/latex/fixtures/tex_expl3_cs_exist_c_cases_native.json","json")^;
let sources = [
  "\\ExplSyntaxOn\\def\\foo{A}\\seq_if_exist:cTF{foo}{Y}{N}\\seq_if_exist:cT{foo}{T}\\seq_if_exist:cF{missing}{F}\\int_eval:n{\\seq_if_exist_p:c{foo}}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\seq_if_exist:cTF\\cs_if_exist:cTF Y\\else N\\fi\\ifx\\seq_if_exist:cT\\cs_if_exist:cT Y\\else N\\fi\\ifx\\seq_if_exist:cF\\cs_if_exist:cF Y\\else N\\fi\\ifx\\seq_if_exist_p:c\\cs_if_exist_p:c Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\int_if_exist:cTF{foo}{Y}{N}\\int_if_exist:cT{foo}{T}\\int_if_exist:cF{missing}{F}\\int_eval:n{\\int_if_exist_p:c{foo}}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\int_if_exist:cTF\\cs_if_exist:cTF Y\\else N\\fi\\ifx\\int_if_exist:cT\\cs_if_exist:cT Y\\else N\\fi\\ifx\\int_if_exist:cF\\cs_if_exist:cF Y\\else N\\fi\\ifx\\int_if_exist_p:c\\cs_if_exist_p:c Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\bool_if_exist:cTF{foo}{Y}{N}\\bool_if_exist:cT{foo}{T}\\bool_if_exist:cF{missing}{F}\\int_eval:n{\\bool_if_exist_p:c{foo}}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\bool_if_exist:cTF\\cs_if_exist:cTF Y\\else N\\fi\\ifx\\bool_if_exist:cT\\cs_if_exist:cT Y\\else N\\fi\\ifx\\bool_if_exist:cF\\cs_if_exist:cF Y\\else N\\fi\\ifx\\bool_if_exist_p:c\\cs_if_exist_p:c Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\clist_if_exist:cTF{foo}{Y}{N}\\clist_if_exist:cT{foo}{T}\\clist_if_exist:cF{missing}{F}\\int_eval:n{\\clist_if_exist_p:c{foo}}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\clist_if_exist:cTF\\cs_if_exist:cTF Y\\else N\\fi\\ifx\\clist_if_exist:cT\\cs_if_exist:cT Y\\else N\\fi\\ifx\\clist_if_exist:cF\\cs_if_exist:cF Y\\else N\\fi\\ifx\\clist_if_exist_p:c\\cs_if_exist_p:c Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\prop_if_exist:cTF{foo}{Y}{N}\\prop_if_exist:cT{foo}{T}\\prop_if_exist:cF{missing}{F}\\int_eval:n{\\prop_if_exist_p:c{foo}}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\prop_if_exist:cTF\\cs_if_exist:cTF Y\\else N\\fi\\ifx\\prop_if_exist:cT\\cs_if_exist:cT Y\\else N\\fi\\ifx\\prop_if_exist:cF\\cs_if_exist:cF Y\\else N\\fi\\ifx\\prop_if_exist_p:c\\cs_if_exist_p:c Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\str_if_exist:cTF{foo}{Y}{N}\\str_if_exist:cT{foo}{T}\\str_if_exist:cF{missing}{F}\\int_eval:n{\\str_if_exist_p:c{foo}}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\str_if_exist:cTF\\cs_if_exist:cTF Y\\else N\\fi\\ifx\\str_if_exist:cT\\cs_if_exist:cT Y\\else N\\fi\\ifx\\str_if_exist:cF\\cs_if_exist:cF Y\\else N\\fi\\ifx\\str_if_exist_p:c\\cs_if_exist_p:c Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\dim_if_exist:cTF{foo}{Y}{N}\\dim_if_exist:cT{foo}{T}\\dim_if_exist:cF{missing}{F}\\int_eval:n{\\dim_if_exist_p:c{foo}}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\dim_if_exist:cTF\\cs_if_exist:cTF Y\\else N\\fi\\ifx\\dim_if_exist:cT\\cs_if_exist:cT Y\\else N\\fi\\ifx\\dim_if_exist:cF\\cs_if_exist:cF Y\\else N\\fi\\ifx\\dim_if_exist_p:c\\cs_if_exist_p:c Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\def\\name{foo}\\cs_if_exist:cTF{\\name}{\\cs_if_exist:cF{missing}{Y}}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\let\\q\\cs_if_exist:cTF\\def\\cs_if_exist:cTF#1#2#3{BAD}\\q{foo}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\cs_if_exist:cTF{foo}{藏α}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}{\\let\\foo\\relax\\cs_if_exist:cTF{foo}{Y}{N}}\\cs_if_exist:cTF{foo}{Y}{N}\\ExplSyntaxOff"
];
compare.check_native_expl_expansions(sources,native.cases)^

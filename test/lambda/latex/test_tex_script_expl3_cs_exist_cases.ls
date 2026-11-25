import compare: ._tex_script_recovery
[for(source in [
  "\\ExplSyntaxOn\\cs_if_exist:NTF\\cs_if_exist:NTF{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\cs_if_exist:NTF\\s__seq{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\cs_if_exist:NTF\\c_zero_int{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\cs_if_exist:NTF\\c_true_bool{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\cs_if_exist:NTF\\exp_stop_f:{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}{\\let\\foo\\relax\\cs_if_exist:NTF\\foo{Y}{N}}\\cs_if_exist:NTF\\foo{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\afterassignment X\\cs_if_exist:NTF\\foo{Y}{N}\\def\\bar{B}\\bar\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\let\\foo\\unknown\\edef\\z{\\cs_if_exist:NTF\\foo{Y}{N}}\\z\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\edef\\z{\\cs_if_exist_p:N\\unknown}\\meaning\\z\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\int_eval:n{1+\\cs_if_exist_p:N\\foo}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\cs_if_exist:NTF\\foo{\\cs_if_exist:NF\\unknown{Y}}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\cs_if_exist:NTF\\foo{藏α}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\let\\f\\cs_if_exist:NF\\f\\foo{N}\\f\\unknown{Y}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\if_cs_exist:N\\ifdefined Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\cs_if_exist:NTF\\l_tmpa_tl{Y}{N}\\ExplSyntaxOff"
]) compare.check_expansion(source,{script_expl3:true})^]

import compare: ._tex_script_recovery
[for(source in [
  "\\ExplSyntaxOn\\def\\foo{A}\\int_eval:n{\\cs_if_free_p:c{foo}+\\cs_if_free_p:c{missing}}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\cs_if_free:cTF{missing}{Y}{N}\\ifcsname missing\\endcsname Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\let\\q\\cs_if_free:cTF\\q{missing}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\edef\\z{\\cs_if_free:NTF\\foo{Y}{N}}\\z\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\cs_if_free:NTF\\cs_if_free:NTF{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\cs_if_free:NTF\\s__seq{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\cs_if_free:NTF\\c_zero_int{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\cs_if_free:NTF\\exp_stop_f:{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}{\\let\\foo\\relax\\cs_if_free:NTF\\foo{Y}{N}}\\cs_if_free:NTF\\foo{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\afterassignment X\\cs_if_free:NTF\\foo{N}{Y}\\def\\bar{B}\\bar\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\edef\\z{\\cs_if_free_p:N\\unknown}\\meaning\\z\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\cs_if_free:cTF{foo}{N}{\\cs_if_free:NT\\missing{Y}}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\cs_if_free:cTF{missing}{藏α}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\let\\q\\cs_if_free:NF\\q\\foo{Y}\\q\\missing{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\int_eval:n{1+\\cs_if_free_p:c{foo}+\\cs_if_free_p:c{missing}}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\cs_if_free:cTF{l_tmpa_tl}{Y}{N}\\ExplSyntaxOff"
]) compare.check_expansion(source,{script_expl3:true})^]

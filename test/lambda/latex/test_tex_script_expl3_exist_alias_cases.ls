import compare: ._tex_script_recovery
[for(source in [
  "\\ExplSyntaxOn\\ifx\\prop_if_exist:NTF\\cs_if_exist:NTF Y\\else N\\fi\\ifx\\prop_if_exist:NT\\cs_if_exist:NT Y\\else N\\fi\\ifx\\prop_if_exist:NF\\cs_if_exist:NF Y\\else N\\fi\\ifx\\prop_if_exist_p:N\\cs_if_exist_p:N Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\let\\q\\prop_if_exist:NTF\\def\\cs_if_exist:NTF#1#2#3{BAD}\\q\\undefined{N}{Y}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\str_if_exist:NTF\\foo{Y}{N}\\str_if_exist:NTF\\missing{N}{Y}\\str_if_exist:NT\\foo{T}\\str_if_exist:NF\\missing{F}\\int_eval:n{\\str_if_exist_p:N\\foo+\\str_if_exist_p:N\\missing}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\str_if_exist:NTF\\cs_if_exist:NTF Y\\else N\\fi\\ifx\\str_if_exist:NT\\cs_if_exist:NT Y\\else N\\fi\\ifx\\str_if_exist:NF\\cs_if_exist:NF Y\\else N\\fi\\ifx\\str_if_exist_p:N\\cs_if_exist_p:N Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\let\\q\\str_if_exist:NTF\\def\\cs_if_exist:NTF#1#2#3{BAD}\\q\\undefined{N}{Y}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\dim_if_exist:NTF\\foo{Y}{N}\\dim_if_exist:NTF\\missing{N}{Y}\\dim_if_exist:NT\\foo{T}\\dim_if_exist:NF\\missing{F}\\int_eval:n{\\dim_if_exist_p:N\\foo+\\dim_if_exist_p:N\\missing}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\dim_if_exist:NTF\\cs_if_exist:NTF Y\\else N\\fi\\ifx\\dim_if_exist:NT\\cs_if_exist:NT Y\\else N\\fi\\ifx\\dim_if_exist:NF\\cs_if_exist:NF Y\\else N\\fi\\ifx\\dim_if_exist_p:N\\cs_if_exist_p:N Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\let\\q\\dim_if_exist:NTF\\def\\cs_if_exist:NTF#1#2#3{BAD}\\q\\undefined{N}{Y}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\meaning\\tl_if_exist:NTF/\\meaning\\tl_if_exist_p:N\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\meaning\\seq_if_exist:NTF/\\meaning\\seq_if_exist_p:N\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\meaning\\int_if_exist:NTF/\\meaning\\int_if_exist_p:N\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\meaning\\bool_if_exist:NTF/\\meaning\\bool_if_exist_p:N\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\meaning\\clist_if_exist:NTF/\\meaning\\clist_if_exist_p:N\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\meaning\\prop_if_exist:NTF/\\meaning\\prop_if_exist_p:N\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\meaning\\str_if_exist:NTF/\\meaning\\str_if_exist_p:N\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\meaning\\dim_if_exist:NTF/\\meaning\\dim_if_exist_p:N\\ExplSyntaxOff"
]) compare.check_expansion(source,{script_expl3:true})^]

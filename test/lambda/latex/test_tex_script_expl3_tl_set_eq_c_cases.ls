import compare: ._tex_script_recovery
[for(source in [
  "\\ExplSyntaxOn\\seq_new:N\\l_a_seq\\seq_put_right:Nn\\l_a_seq{A}\\seq_gset_eq:cN{l_b_seq}\\l_a_seq\\seq_use:Nn\\l_b_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\seq_gset_eq:cN\\tl_gset_eq:cN Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_a_seq\\seq_put_right:Nn\\l_a_seq{A}\\seq_gset_eq:Nc\\l_b_seq{l_a_seq}\\seq_use:Nn\\l_b_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\seq_gset_eq:Nc\\tl_gset_eq:Nc Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_a_seq\\seq_put_right:Nn\\l_a_seq{A}\\seq_gset_eq:cc{l_b_seq}{l_a_seq}\\seq_use:Nn\\l_b_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\seq_gset_eq:cc\\tl_gset_eq:cc Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\meaning\\tl_set_eq:cN/\\meaning\\tl_set_eq:Nc/\\meaning\\tl_set_eq:cc\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\meaning\\seq_gset_eq:cN/\\meaning\\seq_gset_eq:Nc/\\meaning\\seq_gset_eq:cc\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\show\\seq_set_eq:cN\\show\\tl_gset_eq:cc\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_set:Nn\\l_a_tl{A}{\\tl_gset_eq:cN{l_b_tl}\\l_a_tl}\\tl_use:N\\l_b_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_set:Nn\\l_a_tl{A}\\globaldefs=-1 {\\tl_gset_eq:cN{l_b_tl}\\l_a_tl}\\globaldefs=0 \\meaning\\l_b_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_set:Nn\\l_a_tl{A}\\afterassignment X\\tl_set_eq:Nc\\l_b_tl{l_a_tl}\\tl_use:N\\l_b_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_set_eq:Nc\\l_b_tl{missing}\\meaning\\l_b_tl/\\ifcsname missing\\endcsname Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_a_seq\\seq_put_right:Nn\\l_a_seq{A}\\let\\q\\seq_set_eq:cc\\q{l_b_seq}{l_a_seq}\\seq_use:Nn\\l_b_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_set:Nn\\l_a_tl{A}\\def\\prefix{l_a}\\tl_set_eq:Nc\\l_b_tl{\\prefix _tl}\\tl_use:N\\l_b_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_a_seq\\seq_put_right:Nn\\l_a_seq{藏α}\\seq_set_eq:cc{l_b_seq}{l_a_seq}\\seq_use:Nn\\l_b_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_set:Nn\\l_a_tl{A}\\globaldefs=1 {\\tl_set_eq:cc{l_b_tl}{l_a_tl}}\\globaldefs=0 \\tl_use:N\\l_b_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_set:Nn\\l_a_tl{A}\\let\\q\\seq_set_eq:cc\\cs_set_protected:Npn\\tl_set_eq:cc#1#2{OVERRIDE}\\q{l_b_tl}{l_a_tl}\\tl_use:N\\l_b_tl\\ExplSyntaxOff"
]) compare.check_expansion(source,{script_expl3:true})^]

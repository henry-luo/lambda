import compare: ._tex_script_recovery
[for(source in [
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_new:N\\l_b_tl\\tl_set:Nn\\l_a_tl{A}\\edef\\f{\\tl_set_eq:NN\\noexpand\\l_b_tl\\noexpand\\l_a_tl}\\f\\tl_use:N\\l_b_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_set_eq:NN\\l_missing_tl\\other_missing\\meaning\\l_missing_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_set_eq:NN\\l_a_tl\\scan_stop:\\meaning\\l_a_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_a_seq\\seq_new:N\\l_b_seq\\seq_put_right:Nn\\l_a_seq{a}\\seq_set_eq:NN\\l_b_seq\\l_a_seq\\seq_use:Nn\\l_b_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_a_seq\\seq_new:N\\l_b_seq\\seq_put_right:Nn\\l_a_seq{a}{\\seq_gset_eq:NN\\l_b_seq\\l_a_seq}\\seq_use:Nn\\l_b_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_a_seq\\seq_new:N\\l_b_seq\\seq_put_right:Nn\\l_a_seq{a}\\seq_set_eq:NN\\l_b_seq\\l_a_seq\\seq_put_right:Nn\\l_a_seq{b}\\seq_use:Nn\\l_b_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_set:Nn\\l_a_tl{藏α}\\tl_set_eq:NN\\l_b_tl\\l_a_tl\\tl_use:N\\l_b_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_set:Nn\\l_a_tl{A}\\let\\z\\tex_let:D\\z\\l_b_tl=\\l_a_tl\\tl_use:N\\l_b_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\meaning\\scan_stop:/\\ifx\\scan_stop:\\relax Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\tex_let:D\\let Y\\else N\\fi/\\ifx\\tex_global:D\\global Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_set:Nn\\l_a_tl{A}\\let\\copy\\seq_set_eq:NN\\tl_set_eq:NN\\l_b_tl\\l_a_tl\\cs_set_protected:Npn\\tl_set_eq:NN#1#2{OVERRIDE}\\copy\\l_c_tl\\l_a_tl\\tl_use:N\\l_b_tl/\\tl_use:N\\l_c_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_set:Nn\\l_a_tl{A}\\let\\q\\tex_let:D\\global\\q\\l_b_tl=\\l_a_tl\\tl_use:N\\l_b_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_set:Nn\\l_a_tl{A}\\seq_set_eq:NN\\l_a_tl\\l_a_tl\\tl_use:N\\l_a_tl\\ExplSyntaxOff"
]) compare.check_expansion(source,{script_expl3:true})^]

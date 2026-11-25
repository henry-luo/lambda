import compare: ._tex_script_recovery
[for(source in [
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{A}\\seq_gput_left:cn{l_s_seq}{B}\\seq_use:Nn\\l_s_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\meaning\\seq_gput_left:cn\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{A}{\\seq_gput_left:cn{l_s_seq}{B}}\\seq_use:Nn\\l_s_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{A}\\seq_gput_right:cn{l_s_seq}{B}\\seq_use:Nn\\l_s_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\meaning\\seq_gput_right:cn\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{A}{\\seq_gput_right:cn{l_s_seq}{B}}\\seq_use:Nn\\l_s_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_s_tl\\def\\prefix{l_s}\\tl_put_left:cn{\\prefix _tl}{藏α}\\tl_use:N\\l_s_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\let\\q\\seq_put_left:cn\\q{l_s_seq}{A}\\seq_use:Nn\\l_s_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\afterassignment X\\seq_put_right:cn{l_s_seq}{A}\\seq_use:Nn\\l_s_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_s_tl\\edef\\q{\\tl_put_left:cn{l_s_tl}{A}}\\q\\tl_use:N\\l_s_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\show\\tl_put_left:cn\\show\\seq_gput_right:cn\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_put_left:cn{missing}{A}\\expandafter\\meaning\\csname missing\\endcsname\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_s_tl\\globaldefs=1 {\\tl_put_left:cn{l_s_tl}{A}}\\globaldefs=0 \\tl_use:N\\l_s_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\globaldefs=-1 {\\seq_gput_right:cn{l_s_seq}{A}}\\globaldefs=0 [\\seq_use:Nn\\l_s_seq{|}]\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_left:cn{l_s_seq}{{a}{b}}\\seq_put_right:cn{l_s_seq}{}\\seq_use:Nn\\l_s_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\def\\a{A}\\seq_put_right:cn{l_s_seq}{\\a}\\def\\a{B}\\seq_item:Nn\\l_s_seq{1}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_s_tl\\let\\stop\\relax\\catcode`x=12 \\tl_put_left:cn{l_s_tl}{x}\\catcode120=11\\stop\\ifcat\\tl_use:N\\l_s_tl a Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\let\\stop\\relax\\catcode`x=12 \\seq_put_left:cn{l_s_seq}{x}\\catcode120=11\\stop\\ifcat\\seq_item:Nn\\l_s_seq{1}a Y\\else N\\fi\\ExplSyntaxOff"
]) compare.check_expansion(source,{script_expl3:true})^]

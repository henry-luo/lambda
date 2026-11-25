import compare: ._tex_script_recovery

pub fn check(ops) any^ {
    let init="\\prop_new:N\\l_p_prop\\prop_put:Nnn\\l_p_prop{k}{A}\\tl_set:Nn\\l_key_tl{k}\\def\\key{k}";
    compare.check_expl_bodies([for(op in ops)init ++ op ++ "[\\l_tmpa_tl]"])^
}

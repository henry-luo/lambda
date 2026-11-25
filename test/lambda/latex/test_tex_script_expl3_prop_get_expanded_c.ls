import compare: ._tex_script_prop_get_variants

compare.check([
 "\\prop_get:cVN{l_p_prop}\\l_key_tl\\l_tmpa_tl",
 "\\prop_get:cvN{l_p_prop}{l_key_tl}\\l_tmpa_tl",
 "\\prop_get:coN{l_p_prop}{\\key}\\l_tmpa_tl",
 "\\prop_get:ceN{l_p_prop}{\\key}\\l_tmpa_tl",
 "\\prop_get:cxN{l_p_prop}{\\key}\\l_tmpa_tl",
 "\\prop_get:cnc{l_p_prop}{k}{l_tmpa_tl}",
 "\\prop_get:cVNTF{l_p_prop}\\l_key_tl\\l_tmpa_tl{Y}{N}",
 "\\prop_get:cncTF{l_p_prop}{k}{l_tmpa_tl}{Y}{N}"
])^

import compare: ._tex_script_prop_get_variants

compare.check([
 "\\prop_get:NVN\\l_p_prop\\l_key_tl\\l_tmpa_tl",
 "\\prop_get:NvN\\l_p_prop{l_key_tl}\\l_tmpa_tl",
 "\\prop_get:NoN\\l_p_prop{\\key}\\l_tmpa_tl",
 "\\prop_get:NeN\\l_p_prop{\\key}\\l_tmpa_tl",
 "\\prop_get:NxN\\l_p_prop{\\key}\\l_tmpa_tl",
 "\\prop_get:NVNTF\\l_p_prop\\l_key_tl\\l_tmpa_tl{Y}{N}"
])^

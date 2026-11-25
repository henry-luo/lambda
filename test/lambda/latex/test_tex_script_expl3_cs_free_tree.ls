import compare: ._tex_script_recovery
[for(source in [
 "\\ExplSyntaxOn\\cs_if_free:NTF\\unknown{\\def\\foo{Y}}{\\def\\foo{N}}\\ExplSyntaxOff\\begin{document}\\foo\\end{document}",
 "\\ExplSyntaxOn\\cs_if_free:cTF{unknown}{Y}{N}\\ExplSyntaxOff\\begin{document}Hello\\end{document}",
 "\\ExplSyntaxOn\\let\\q\\cs_if_free:NTF\\q\\unknown{Y}{N}\\ExplSyntaxOff\\begin{document}Hello\\end{document}",
 "\\ExplSyntaxOn\\let\\q\\cs_if_free:cTF\\q{unknown}{Y}{N}\\ExplSyntaxOff\\begin{document}Hello\\end{document}"
]) compare.check_tree(source,{script_expl3:true})^]
